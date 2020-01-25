/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2020 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
* Copyright (C) 2014 - 2017 Jan Bajer aka bajasoft <jbajer@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*
**************************************************************************/

#include "ContentFiltersViewWidget.h"
#include "Animation.h"
#include "preferences/ContentBlockingProfileDialog.h"
#include "../core/ContentFiltersManager.h"
#include "../core/SettingsManager.h"
#include "../core/ThemesManager.h"
#include "../core/Utils.h"

#include <QtCore/QTimer>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QToolTip>

namespace Otter
{

ContentFiltersTitleDelegate::ContentFiltersTitleDelegate(QObject *parent) : ItemDelegate({{ItemDelegate::ProgressHasErrorRole, ContentFiltersViewWidget::HasErrorRole}, {ItemDelegate::ProgressHasIndicatorRole, ContentFiltersViewWidget::IsShowingProgressIndicatorRole}, {ItemDelegate::ProgressValueRole, ContentFiltersViewWidget::UpdateProgressValueRole}}, parent)
{
}

void ContentFiltersTitleDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
	ItemDelegate::initStyleOption(option, index);

	if (index.parent().isValid())
	{
		option->features |= QStyleOptionViewItem::HasDecoration;

		if (index.data(ContentFiltersViewWidget::IsUpdatingRole).toBool())
		{
			const Animation *animation(ContentFiltersViewWidget::getUpdateAnimation());

			if (animation)
			{
				option->icon = QIcon(animation->getCurrentPixmap());
			}
		}
		else if (index.data(ContentFiltersViewWidget::HasErrorRole).toBool())
		{
			option->icon = ThemesManager::createIcon(QLatin1String("dialog-error"));
		}
		else if (index.data(ContentFiltersViewWidget::UpdateTimeRole).isNull() || index.data(ContentFiltersViewWidget::UpdateTimeRole).toDateTime().daysTo(QDateTime::currentDateTimeUtc()) > 7)
		{
			option->icon = ThemesManager::createIcon(QLatin1String("dialog-warning"));
		}
	}
}

bool ContentFiltersTitleDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index)
{
	if (event->type() == QEvent::ToolTip)
	{
		const ContentFiltersProfile *profile(ContentFiltersManager::getProfile(index.data(ContentFiltersViewWidget::NameRole).toString()));

		if (profile)
		{
			QString toolTip;

			if (profile->getError() != ContentFiltersProfile::NoError)
			{
				switch (profile->getError())
				{
					case ContentFiltersProfile::ReadError:
						toolTip = tr("Failed to read profile file");

						break;
					case ContentFiltersProfile::DownloadError:
						toolTip = tr("Failed to download profile rules");

						break;
					case ContentFiltersProfile::ChecksumError:
						toolTip = tr("Failed to verify profile rules using checksum");

						break;
					default:
						break;
				}
			}
			else if (profile->getLastUpdate().isNull())
			{
				toolTip = tr("Profile was never updated");
			}
			else if (profile->getLastUpdate().daysTo(QDateTime::currentDateTimeUtc()) > 7)
			{
				toolTip = tr("Profile was last updated more than one week ago");
			}

			if (!toolTip.isEmpty())
			{
				QToolTip::showText(event->globalPos(), displayText(index.data(Qt::DisplayRole), view->locale()) + QLatin1Char('\n') + toolTip, view);

				return true;
			}
		}
	}

	return ItemDelegate::helpEvent(event, view, option, index);
}

ContentFiltersIntervalDelegate::ContentFiltersIntervalDelegate(QObject *parent) : ItemDelegate(parent)
{
}

void ContentFiltersIntervalDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
	const QSpinBox *widget(qobject_cast<QSpinBox*>(editor));

	if (widget)
	{
		model->setData(index, widget->value(), Qt::DisplayRole);
	}
}

QWidget* ContentFiltersIntervalDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	Q_UNUSED(option)

	QSpinBox *widget(new QSpinBox(parent));
	widget->setSuffix(QCoreApplication::translate("Otter::ContentBlockingIntervalDelegate", " day(s)"));
	widget->setSpecialValueText(QCoreApplication::translate("Otter::ContentBlockingIntervalDelegate", "Never"));
	widget->setMinimum(0);
	widget->setMaximum(365);
	widget->setValue(index.data(Qt::DisplayRole).toInt());
	widget->setFocus();

	return widget;
}

QString ContentFiltersIntervalDelegate::displayText(const QVariant &value, const QLocale &locale) const
{
	Q_UNUSED(locale)

	if (value.isNull())
	{
		return {};
	}

	const int updateInterval(value.toInt());

	return ((updateInterval > 0) ? QCoreApplication::translate("Otter::ContentBlockingIntervalDelegate", "%n day(s)", "", updateInterval) : QCoreApplication::translate("Otter::ContentBlockingIntervalDelegate", "Never"));
}

Animation* ContentFiltersViewWidget::m_updateAnimation = nullptr;

ContentFiltersViewWidget::ContentFiltersViewWidget(QWidget *parent) : ItemViewWidget(parent)
{
	setViewMode(ItemViewWidget::TreeView);
	setItemDelegateForColumn(0, new ContentFiltersTitleDelegate(this));
	setItemDelegateForColumn(1, new ContentFiltersIntervalDelegate(this));
	getViewportWidget()->setUpdateDataRole(ContentFiltersViewWidget::IsShowingProgressIndicatorRole);

	connect(ContentFiltersManager::getInstance(), &ContentFiltersManager::profileModified, this, &ContentFiltersViewWidget::handleProfileModified);
}

void ContentFiltersViewWidget::contextMenuEvent(QContextMenuEvent *event)
{
	const QModelIndex index(currentIndex().sibling(currentIndex().row(), 0));
	QMenu menu(this);
	menu.addAction(tr("Add…"), this, &ContentFiltersViewWidget::addProfile);

	if (index.isValid() && index.flags().testFlag(Qt::ItemNeverHasChildren))
	{
		menu.addSeparator();
		menu.addAction(tr("Edit…"), this, &ContentFiltersViewWidget::editProfile);
		menu.addAction(tr("Update"), this, &ContentFiltersViewWidget::updateProfile)->setEnabled(index.isValid() && index.data(ContentFiltersViewWidget::UpdateUrlRole).toUrl().isValid());
		menu.addSeparator();
		menu.addAction(ThemesManager::createIcon(QLatin1String("edit-delete")), tr("Remove"), this, &ContentFiltersViewWidget::removeProfile);
	}

	menu.exec(event->globalPos());
}

void ContentFiltersViewWidget::addProfile()
{
	ContentBlockingProfileDialog dialog(this);

	if (dialog.exec() == QDialog::Accepted)
	{
		updateModel(dialog.getProfile(), true);
	}
}

void ContentFiltersViewWidget::editProfile()
{
	const QModelIndex index(currentIndex().sibling(currentIndex().row(), 0));
	ContentFiltersProfile *profile(ContentFiltersManager::getProfile(index.data(ContentFiltersViewWidget::NameRole).toString()));

	if (profile)
	{
		const ContentFiltersProfile::ProfileCategory category(profile->getCategory());
		ContentBlockingProfileDialog dialog(this, profile);

		if (dialog.exec() == QDialog::Accepted)
		{
			updateModel(profile, (category != profile->getCategory()));
		}
	}
}

void ContentFiltersViewWidget::removeProfile()
{
	const QModelIndex index(currentIndex().sibling(currentIndex().row(), 0));
	ContentFiltersProfile *profile(ContentFiltersManager::getProfile(index.data(ContentFiltersViewWidget::NameRole).toString()));

	if (!profile)
	{
		return;
	}

	QMessageBox messageBox;
	messageBox.setWindowTitle(tr("Question"));
	messageBox.setText(tr("Do you really want to remove this profile?"));
	messageBox.setIcon(QMessageBox::Question);
	messageBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
	messageBox.setDefaultButton(QMessageBox::Cancel);

	if (QFile::exists(profile->getPath()))
	{
		messageBox.setCheckBox(new QCheckBox(tr("Delete profile permanently")));
	}

	if (messageBox.exec() == QMessageBox::Yes)
	{
		ContentFiltersManager::removeProfile(profile, (messageBox.checkBox() && messageBox.checkBox()->isChecked()));

		model()->removeRow(index.row(), index.parent());

		if (getRowCount(index.parent()) == 0)
		{
			model()->removeRow(index.parent().row(), index.parent().parent());
		}

		clearSelection();
	}
}

void ContentFiltersViewWidget::updateProfile()
{
	const QModelIndex index(currentIndex().sibling(currentIndex().row(), 0));
	ContentFiltersProfile *profile(ContentFiltersManager::getProfile(index.data(ContentFiltersViewWidget::NameRole).toString()));

	if (!profile)
	{
		return;
	}

	if (!m_updateAnimation)
	{
		const QString path(ThemesManager::getAnimationPath(QLatin1String("spinner")));

		if (path.isEmpty())
		{
			m_updateAnimation = new SpinnerAnimation(QCoreApplication::instance());
		}
		else
		{
			m_updateAnimation = new GenericAnimation(path, QCoreApplication::instance());
		}

		m_updateAnimation->start();

		getViewportWidget()->updateDirtyIndexesList();
	}

	profile->update();
}

void ContentFiltersViewWidget::updateModel(ContentFiltersProfile *profile, bool isNewOrMoved)
{
	if (isNewOrMoved)
	{
		const QModelIndexList removeList(model()->match(model()->index(0, 0), ContentFiltersViewWidget::NameRole, QVariant::fromValue(profile->getName()), 2, Qt::MatchRecursive));

		if (removeList.count() > 0)
		{
			model()->removeRow(removeList[0].row(), removeList[0].parent());
		}

		QList<QStandardItem*> profileItems({new QStandardItem(profile->getTitle()), new QStandardItem(QString::number(profile->getUpdateInterval())), new QStandardItem(Utils::formatDateTime(profile->getLastUpdate()))});
		profileItems[0]->setData(profile->getName(), ContentFiltersViewWidget::NameRole);
		profileItems[0]->setData(profile->getUpdateUrl(), ContentFiltersViewWidget::UpdateUrlRole);
		profileItems[0]->setFlags(Qt::ItemNeverHasChildren | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
		profileItems[0]->setCheckable(true);
		profileItems[0]->setCheckState(Qt::Unchecked);
		profileItems[1]->setFlags(Qt::ItemNeverHasChildren | Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
		profileItems[2]->setFlags(Qt::ItemNeverHasChildren | Qt::ItemIsSelectable | Qt::ItemIsEnabled);

		const QModelIndexList indexList(model()->match(model()->index(0, 0), ContentFiltersViewWidget::NameRole, QVariant::fromValue(static_cast<int>(profile->getCategory()))));

		if (indexList.count() > 0)
		{
			QStandardItem *categoryItem(qobject_cast<QStandardItemModel* >(model())->itemFromIndex(indexList[0]));
			categoryItem->appendRow(profileItems);
			categoryItem->sortChildren(getSortColumn(), getSortOrder());
		}

		return;
	}

	const QModelIndex currentIndex(this->currentIndex());

	setData(currentIndex.sibling(currentIndex.row(), 0), profile->getTitle(), Qt::DisplayRole);
	setData(currentIndex.sibling(currentIndex.row(), 0), profile->getUpdateUrl(), ContentFiltersViewWidget::UpdateUrlRole);
	setData(currentIndex.sibling(currentIndex.row(), 1), profile->getUpdateInterval(), Qt::DisplayRole);
	setData(currentIndex.sibling(currentIndex.row(), 2), Utils::formatDateTime(profile->getLastUpdate()), Qt::DisplayRole);
}

void ContentFiltersViewWidget::handleProfileModified(const QString &name)
{
	const ContentFiltersProfile *profile(ContentFiltersManager::getProfile(name));

	if (!profile)
	{
		return;
	}

	QStandardItem *entryItem(nullptr);

	for (int i = 0; i < getRowCount(); ++i)
	{
		const QModelIndex categoryIndex(getIndex(i));
		bool hasFound(false);

		for (int j = 0; j < getRowCount(categoryIndex); ++j)
		{
			const QModelIndex entryIndex(getIndex(j, 0, categoryIndex));

			if (entryIndex.data(ContentFiltersViewWidget::NameRole).toString() == name)
			{
				QString title(profile->getTitle());

				entryItem = getSourceModel()->itemFromIndex(entryIndex);
				hasFound = true;

				if (profile->getCategory() == ContentFiltersProfile::RegionalCategory)
				{
					const QVector<QLocale::Language> languages(profile->getLanguages());
					QStringList languageNames;
					languageNames.reserve(languages.count());

					for (int k = 0; k < languages.count(); ++k)
					{
						languageNames.append(QLocale::languageToString(languages.at(k)));
					}

					title = QStringLiteral("%1 [%2]").arg(title, languageNames.join(QLatin1String(", ")));
				}

				setData(entryIndex, title, Qt::DisplayRole);
				setData(entryIndex.sibling(j, 2), Utils::formatDateTime(profile->getLastUpdate()), Qt::DisplayRole);

				break;
			}
		}

		if (hasFound)
		{
			break;
		}
	}

	if (entryItem)
	{
		const bool hasError(profile->getError() != ContentFiltersProfile::NoError);

		entryItem->setData(hasError, ContentFiltersViewWidget::HasErrorRole);
		entryItem->setData(profile->getLastUpdate(), ContentFiltersViewWidget::UpdateTimeRole);

		if (profile->isUpdating() != entryItem->data(ContentFiltersViewWidget::IsUpdatingRole).toBool())
		{
			entryItem->setData(profile->isUpdating(), ContentFiltersViewWidget::IsUpdatingRole);

			if (profile->isUpdating())
			{
				entryItem->setData(true, ContentFiltersViewWidget::IsShowingProgressIndicatorRole);

				connect(profile, &ContentFiltersProfile::updateProgressChanged, profile, [=](int progress)
				{
					entryItem->setData(((progress < 0 && entryItem->data(ContentFiltersViewWidget::HasErrorRole).toBool()) ? 0 : progress), ContentFiltersViewWidget::UpdateProgressValueRole);
				});
			}
			else
			{
				if (entryItem->data(ContentFiltersViewWidget::UpdateProgressValueRole).toInt() < 0)
				{
					entryItem->setData((hasError ? 0 : 100), ContentFiltersViewWidget::UpdateProgressValueRole);
				}

				QTimer::singleShot(2500, this, [=]()
				{
					if (!profile->isUpdating())
					{
						entryItem->setData(false, ContentFiltersViewWidget::IsShowingProgressIndicatorRole);
					}
				});
			}
		}
	}

	viewport()->update();
}

void ContentFiltersViewWidget::setSelectedProfiles(const QStringList &profiles)
{
	if (model())
	{
		model()->deleteLater();
	}

	QHash<ContentFiltersProfile::ProfileCategory, QMultiMap<QString, QList<QStandardItem*> > > categoryEntries;
	const QVector<ContentFiltersProfile*> contentBlockingProfiles(ContentFiltersManager::getContentBlockingProfiles());
	QStandardItemModel *model(new QStandardItemModel(this));
	model->setHorizontalHeaderLabels({tr("Title"), tr("Update Interval"), tr("Last Update")});
	model->setHeaderData(0, Qt::Horizontal, 250, HeaderViewWidget::WidthRole);

	for (int i = 0; i < contentBlockingProfiles.count(); ++i)
	{
		const ContentFiltersProfile *profile(contentBlockingProfiles.at(i));
		const QString name(profile->getName());

		if (name == QLatin1String("custom"))
		{
			continue;
		}

		const ContentFiltersProfile::ProfileCategory category(profile->getCategory());
		QString title(profile->getTitle());

		if (category == ContentFiltersProfile::RegionalCategory)
		{
			const QVector<QLocale::Language> languages(profile->getLanguages());
			QStringList languageNames;
			languageNames.reserve(languages.count());

			for (int j = 0; j < languages.count(); ++j)
			{
				languageNames.append(QLocale::languageToString(languages.at(j)));
			}

			title = QStringLiteral("%1 [%2]").arg(title, languageNames.join(QLatin1String(", ")));
		}

		QList<QStandardItem*> profileItems({new QStandardItem(title), new QStandardItem(QString::number(profile->getUpdateInterval())), new QStandardItem(Utils::formatDateTime(profile->getLastUpdate()))});
		profileItems[0]->setData(name, NameRole);
		profileItems[0]->setData(false, HasErrorRole);
		profileItems[0]->setData(false, IsShowingProgressIndicatorRole);
		profileItems[0]->setData(false, IsUpdatingRole);
		profileItems[0]->setData(-1, UpdateProgressValueRole);
		profileItems[0]->setData(profile->getLastUpdate(), UpdateTimeRole);
		profileItems[0]->setData(profile->getUpdateUrl(), UpdateUrlRole);
		profileItems[0]->setFlags(Qt::ItemNeverHasChildren | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
		profileItems[0]->setCheckable(true);
		profileItems[0]->setCheckState(profiles.contains(name) ? Qt::Checked : Qt::Unchecked);
		profileItems[1]->setFlags(Qt::ItemNeverHasChildren | Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
		profileItems[2]->setFlags(Qt::ItemNeverHasChildren | Qt::ItemIsSelectable | Qt::ItemIsEnabled);

		if (!categoryEntries.contains(category))
		{
			categoryEntries[category] = {};
		}

		categoryEntries[category].insert(title, profileItems);
	}

	const QVector<QPair<ContentFiltersProfile::ProfileCategory, QString> > categoryTitles({{ContentFiltersProfile::AdvertisementsCategory, tr("Advertisements")}, {ContentFiltersProfile::AnnoyanceCategory, tr("Annoyance")}, {ContentFiltersProfile::PrivacyCategory, tr("Privacy")}, {ContentFiltersProfile::SocialCategory, tr("Social")}, {ContentFiltersProfile::RegionalCategory, tr("Regional")}, {ContentFiltersProfile::OtherCategory, tr("Other")}});

	for (int i = 0; i < categoryTitles.count(); ++i)
	{
		if (!categoryEntries.contains(categoryTitles.at(i).first))
		{
			continue;
		}

		const QList<QList<QStandardItem*> > profileItems(categoryEntries[categoryTitles.at(i).first].values());
		QList<QStandardItem*> categoryItems({new QStandardItem(categoryTitles.at(i).second), new QStandardItem(), new QStandardItem()});
		categoryItems[0]->setData(false, IsShowingProgressIndicatorRole);
		categoryItems[0]->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
		categoryItems[1]->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
		categoryItems[2]->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

		for (int j = 0; j < profileItems.count(); ++j)
		{
			categoryItems[0]->appendRow(profileItems.at(j));
		}

		model->appendRow(categoryItems);
	}

	setModel(model);

	expandAll();
}

Animation* ContentFiltersViewWidget::getUpdateAnimation()
{
	return m_updateAnimation;
}

}
