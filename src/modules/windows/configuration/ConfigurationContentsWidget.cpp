/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2017 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
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

#include "ConfigurationContentsWidget.h"
#include "../../../core/ActionsManager.h"
#include "../../../core/NetworkManagerFactory.h"
#include "../../../core/SettingsManager.h"
#include "../../../core/ThemesManager.h"
#include "../../../ui/ColorWidget.h"
#include "../../../ui/OptionWidget.h"

#include "ui_ConfigurationContentsWidget.h"

#include <QtCore/QMetaEnum>
#include <QtCore/QSettings>
#include <QtGui/QClipboard>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>

namespace Otter
{

ConfigurationOptionDelegate::ConfigurationOptionDelegate(QObject *parent) : ItemDelegate(parent)
{
}

void ConfigurationOptionDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
	ItemDelegate::initStyleOption(option, index);

	const SettingsManager::OptionDefinition definition(SettingsManager::getOptionDefinition(index.data(ConfigurationContentsWidget::IdentifierRole).toInt()));

	option->text = SettingsManager::createDisplayValue(definition.identifier, index.data(Qt::DisplayRole));

	switch (definition.type)
	{
		case SettingsManager::ColorType:
			{
				QPixmap pixmap(option->fontMetrics.height(), option->fontMetrics.height());
				pixmap.fill(Qt::transparent);

				QPainter painter(&pixmap);
				painter.setRenderHints(QPainter::Antialiasing);

				ColorWidget::drawThumbnail(&painter, QColor(index.data(Qt::DisplayRole).toString()), option->palette, pixmap.rect());

				painter.end();

				option->features |= QStyleOptionViewItem::HasDecoration;
				option->decorationSize = pixmap.size();
				option->icon = QIcon(pixmap);
			}

			break;
		case SettingsManager::EnumerationType:
			{
				const QString value(index.data(Qt::DisplayRole).toString());

				if (definition.hasIcons())
				{
					option->features |= QStyleOptionViewItem::HasDecoration;
				}

				for (int i = 0; i < definition.choices.count(); ++i)
				{
					if (definition.choices.at(i).value == value)
					{
						option->icon = definition.choices.at(i).icon;

						break;
					}
				}
			}

			break;
		case SettingsManager::FontType:
			option->font = QFont(index.data(Qt::DisplayRole).toString());

			break;
		default:
			break;
	}
}

void ConfigurationOptionDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
	OptionWidget *widget(qobject_cast<OptionWidget*>(editor));

	if (widget && !index.sibling(index.row(), 0).data(ConfigurationContentsWidget::IsModifiedRole).toBool())
	{
		widget->setValue(index.data(Qt::EditRole));
	}
}

void ConfigurationOptionDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
	const OptionWidget *widget(qobject_cast<OptionWidget*>(editor));

	if (widget)
	{
		const QModelIndex optionIndex(index.sibling(index.row(), 0));
		QFont font(optionIndex.data(Qt::FontRole).value<QFont>());
		font.setBold(widget->getValue() != widget->getDefaultValue());

		model->setData(index, widget->getValue(), Qt::EditRole);
		model->setData(optionIndex, font, Qt::FontRole);
		model->setData(optionIndex, true, ConfigurationContentsWidget::IsModifiedRole);
	}
}

QWidget* ConfigurationOptionDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	Q_UNUSED(option)

	const SettingsManager::OptionDefinition definition(SettingsManager::getOptionDefinition(index.data(ConfigurationContentsWidget::IdentifierRole).toInt()));
	OptionWidget *widget(new OptionWidget(index.data(Qt::EditRole), definition.type, parent));
	widget->setDefaultValue(definition.defaultValue);
	widget->setFocus();

	if (definition.type == SettingsManager::EnumerationType)
	{
		widget->setChoices(definition.choices);
	}

	connect(widget, &OptionWidget::commitData, this, &ConfigurationOptionDelegate::commitData);

	return widget;
}

ConfigurationContentsWidget::ConfigurationContentsWidget(const QVariantMap &parameters, Window *window, QWidget *parent) : ContentsWidget(parameters, window, parent),
	m_model(new QStandardItemModel(this)),
	m_ui(new Ui::ConfigurationContentsWidget)
{
	m_ui->setupUi(this);

	NetworkManagerFactory::initialize();

	const QMetaEnum metaEnum(SettingsManager::getInstance()->metaObject()->enumerator(SettingsManager::getInstance()->metaObject()->indexOfEnumerator(QLatin1String("OptionType").data())));
	const QStringList options(SettingsManager::getOptions());
	QStandardItem *groupItem(nullptr);
	bool canResetAll(false);

	for (int i = 0; i < options.count(); ++i)
	{
		const int identifier(SettingsManager::getOptionIdentifier(options.at(i)));
		const QStringList option(options.at(i).split(QLatin1Char('/')));
		const QVariant value(SettingsManager::getOption(identifier));
		const SettingsManager::OptionDefinition definition(SettingsManager::getOptionDefinition(identifier));

		if (!definition.flags.testFlag(SettingsManager::IsEnabledFlag) || !definition.flags.testFlag(SettingsManager::IsVisibleFlag))
		{
			continue;
		}

		if (!groupItem || groupItem->text() != option.first())
		{
			groupItem = new QStandardItem(ThemesManager::createIcon(QLatin1String("inode-directory")), option.first());

			m_model->appendRow(groupItem);
		}

		QString type(metaEnum.valueToKey(definition.type));
		type.chop(4);

		QList<QStandardItem*> optionItems({new QStandardItem(option.last()), new QStandardItem(type.toLower()), new QStandardItem((value.type() == QVariant::StringList) ? value.toStringList().join(QLatin1String(", ")) : value.toString())});
		optionItems[0]->setFlags(optionItems[0]->flags() | Qt::ItemNeverHasChildren);
		optionItems[1]->setFlags(optionItems[1]->flags() | Qt::ItemNeverHasChildren);
		optionItems[2]->setData(QSize(-1, 30), Qt::SizeHintRole);
		optionItems[2]->setData(identifier, IdentifierRole);
		optionItems[2]->setData(options.at(i), NameRole);
		optionItems[2]->setFlags(optionItems[2]->flags() | Qt::ItemNeverHasChildren);

		if (value != definition.defaultValue)
		{
			QFont font(optionItems[0]->font());
			font.setBold(true);

			optionItems[0]->setFont(font);

			if (identifier != SettingsManager::Browser_MigrationsOption)
			{
				canResetAll = true;
			}
		}

		groupItem->appendRow(optionItems);
	}

	m_model->setHorizontalHeaderLabels(QStringList({tr("Name"), tr("Type"), tr("Value")}));
	m_model->sort(0);

	m_ui->configurationViewWidget->setViewMode(ItemViewWidget::TreeViewMode);
	m_ui->configurationViewWidget->setModel(m_model);
	m_ui->configurationViewWidget->setLayoutDirection(Qt::LeftToRight);
	m_ui->configurationViewWidget->setItemDelegateForColumn(2, new ConfigurationOptionDelegate(this));
	m_ui->configurationViewWidget->setFilterRoles({Qt::DisplayRole, NameRole});
	m_ui->configurationViewWidget->installEventFilter(this);
	m_ui->filterLineEditWidget->installEventFilter(this);
	m_ui->resetAllButton->setEnabled(canResetAll);

	if (isSidebarPanel())
	{
		m_ui->detailsWidget->hide();
	}

	connect(SettingsManager::getInstance(), &SettingsManager::optionChanged, this, &ConfigurationContentsWidget::handleOptionChanged);
	connect(m_ui->configurationViewWidget, &ItemViewWidget::customContextMenuRequested, this, &ConfigurationContentsWidget::showContextMenu);
	connect(m_ui->configurationViewWidget, &ItemViewWidget::needsActionsUpdate, this, &ConfigurationContentsWidget::updateActions);
	connect(m_ui->configurationViewWidget, &ItemViewWidget::clicked, this, &ConfigurationContentsWidget::handleIndexClicked);
	connect(m_ui->configurationViewWidget, &ItemViewWidget::modified, [&]()
	{
		m_ui->resetAllButton->setEnabled(true);
		m_ui->saveAllButton->setEnabled(true);
	});
	connect(m_ui->configurationViewWidget->selectionModel(), &QItemSelectionModel::currentChanged, this, &ConfigurationContentsWidget::handleCurrentIndexChanged);
	connect(m_ui->filterLineEditWidget, &LineEditWidget::textChanged, m_ui->configurationViewWidget, &ItemViewWidget::setFilterString);
	connect(m_ui->resetAllButton, &QPushButton::clicked, [&]()
	{
		saveAll(true);
	});
	connect(m_ui->saveAllButton, &QPushButton::clicked, [&]()
	{
		saveAll(false);
	});
}

ConfigurationContentsWidget::~ConfigurationContentsWidget()
{
	delete m_ui;
}

void ConfigurationContentsWidget::changeEvent(QEvent *event)
{
	ContentsWidget::changeEvent(event);

	if (event->type() == QEvent::LanguageChange)
	{
		m_ui->retranslateUi(this);
	}
}

void ConfigurationContentsWidget::triggerAction(int identifier, const QVariantMap &parameters)
{
	switch (identifier)
	{
		case ActionsManager::FindAction:
		case ActionsManager::QuickFindAction:
			m_ui->filterLineEditWidget->setFocus();

			break;
		case ActionsManager::ActivateContentAction:
			m_ui->configurationViewWidget->setFocus();

			break;
		default:
			ContentsWidget::triggerAction(identifier, parameters);

			break;
	}
}

void ConfigurationContentsWidget::closeEvent(QCloseEvent *event)
{
	if (m_ui->configurationViewWidget->isModified())
	{
		const int result(QMessageBox::question(this, tr("Question"), tr("The settings have been changed.\nDo you want to save them?"), QMessageBox::Yes, QMessageBox::No, QMessageBox::Cancel));

		if (result == QMessageBox::Cancel)
		{
			event->ignore();

			return;
		}

		if (result == QMessageBox::Yes)
		{
			saveAll(false);
		}
	}

	event->accept();
}

void ConfigurationContentsWidget::print(QPrinter *printer)
{
	m_ui->configurationViewWidget->render(printer);
}

void ConfigurationContentsWidget::copyOptionName()
{
	const QModelIndex index(m_ui->configurationViewWidget->currentIndex().sibling(m_ui->configurationViewWidget->currentIndex().row(), 2));

	if (index.isValid())
	{
		QApplication::clipboard()->setText(index.data(NameRole).toString());
	}
}

void ConfigurationContentsWidget::copyOptionValue()
{
	const QModelIndex index(m_ui->configurationViewWidget->currentIndex().sibling(m_ui->configurationViewWidget->currentIndex().row(), 2));

	if (index.isValid())
	{
		QApplication::clipboard()->setText(index.data(Qt::EditRole).toString());
	}
}

void ConfigurationContentsWidget::resetOption()
{
	const QModelIndex index(m_ui->configurationViewWidget->currentIndex().sibling(m_ui->configurationViewWidget->currentIndex().row(), 2));

	if (index.isValid())
	{
		SettingsManager::setOption(index.data(IdentifierRole).toInt(), SettingsManager::getOptionDefinition(index.data(IdentifierRole).toInt()).defaultValue);

		m_model->setData(index.sibling(index.row(), 0), false, IsModifiedRole);

		updateActions();
	}
}

void ConfigurationContentsWidget::saveOption()
{
	const QModelIndex index(m_ui->configurationViewWidget->currentIndex().sibling(m_ui->configurationViewWidget->currentIndex().row(), 2));

	if (index.isValid())
	{
		SettingsManager::setOption(index.data(IdentifierRole).toInt(), index.data(Qt::EditRole));

		m_model->setData(index.sibling(index.row(), 0), false, IsModifiedRole);

		updateActions();
	}
}

void ConfigurationContentsWidget::saveAll(bool reset)
{
	if (reset && QMessageBox::question(this, tr("Question"), tr("Do you really want to restore default values of all options?"), QMessageBox::Yes, QMessageBox::No) == QMessageBox::No)
	{
		return;
	}

	for (int i = 0; i < m_model->rowCount(); ++i)
	{
		const QModelIndex groupIndex(m_model->index(i, 0));
		const int optionAmount(m_model->rowCount(groupIndex));

		if (optionAmount == 0)
		{
			continue;
		}

		for (int j = 0; j < optionAmount; ++j)
		{
			const QModelIndex optionIndex(groupIndex.child(j, 0));
			const bool isModified(optionIndex.data(IsModifiedRole).toBool());

			if (reset || isModified)
			{
				const QModelIndex valueIndex(groupIndex.child(j, 2));
				const int identifier(valueIndex.data(IdentifierRole).toInt());
				const QVariant defaultValue(SettingsManager::getOptionDefinition(identifier).defaultValue);

				if (reset && identifier != SettingsManager::Browser_MigrationsOption && valueIndex.data(Qt::EditRole) != defaultValue)
				{
					SettingsManager::setOption(identifier, defaultValue);

					QFont font(optionIndex.data(Qt::FontRole).isNull() ? m_ui->configurationViewWidget->font() : optionIndex.data(Qt::FontRole).value<QFont>());
					font.setBold(false);

					m_model->setData(optionIndex, font, Qt::FontRole);
					m_model->setData(valueIndex, defaultValue, Qt::EditRole);
				}
				else if (!reset && isModified)
				{
					SettingsManager::setOption(identifier, valueIndex.data(Qt::EditRole));
				}

				if (isModified)
				{
					m_model->setData(optionIndex, false, IsModifiedRole);
				}
			}
		}
	}

	m_ui->configurationViewWidget->setModified(false);
	m_ui->saveAllButton->setEnabled(false);

	if (reset)
	{
		m_ui->resetAllButton->setEnabled(false);
	}

	connect(m_ui->configurationViewWidget, &ItemViewWidget::needsActionsUpdate, this, &ConfigurationContentsWidget::updateActions);

	updateActions();
}

void ConfigurationContentsWidget::handleOptionChanged(int identifier, const QVariant &value)
{
	const QString name(SettingsManager::getOptionName(identifier));
	const bool wasModified(m_ui->configurationViewWidget->isModified());

	for (int i = 0; i < m_model->rowCount(); ++i)
	{
		const QModelIndex groupIndex(m_model->index(i, 0));
		const QString groupTitle(groupIndex.data(Qt::DisplayRole).toString());

		if (groupTitle.isEmpty() || !name.startsWith(groupTitle))
		{
			continue;
		}

		const int optionAmount(m_model->rowCount(groupIndex));

		for (int j = 0; j < optionAmount; ++j)
		{
			const QModelIndex valueIndex(groupIndex.child(j, 2));

			if (valueIndex.data(IdentifierRole).toInt() == identifier)
			{
				const QModelIndex optionIndex(groupIndex.child(j, 0));

				if (!optionIndex.data(IsModifiedRole).toBool())
				{
					QFont font(optionIndex.data(Qt::FontRole).isNull() ? m_ui->configurationViewWidget->font() : optionIndex.data(Qt::FontRole).value<QFont>());
					font.setBold(value != SettingsManager::getOptionDefinition(identifier).defaultValue);

					m_model->setData(optionIndex, font, Qt::FontRole);
					m_model->setData(valueIndex, value, Qt::EditRole);
				}

				break;
			}
		}
	}

	if (!wasModified)
	{
		m_ui->configurationViewWidget->setModified(false);
	}

	updateActions();
}

void ConfigurationContentsWidget::handleCurrentIndexChanged(const QModelIndex &currentIndex, const QModelIndex &previousIndex)
{
	if (previousIndex.parent().isValid() && previousIndex.column() == 2)
	{
		m_ui->configurationViewWidget->closePersistentEditor(previousIndex);
	}

	if (currentIndex.parent().isValid() && currentIndex.column() == 2)
	{
		m_ui->configurationViewWidget->openPersistentEditor(currentIndex);
	}
}

void ConfigurationContentsWidget::handleIndexClicked(const QModelIndex &index)
{
	if (index.parent().isValid() && index.column() != 2)
	{
		m_ui->configurationViewWidget->setCurrentIndex(index.sibling(index.row(), 2));
	}
}

void ConfigurationContentsWidget::showContextMenu(const QPoint &position)
{
	const QModelIndex index(m_ui->configurationViewWidget->indexAt(position));
	QMenu menu(this);

	if (index.isValid() && index.parent() != m_ui->configurationViewWidget->rootIndex())
	{
		menu.addAction(tr("Copy Option Name"), this, SLOT(copyOptionName()));
		menu.addAction(tr("Copy Option Value"), this, SLOT(copyOptionValue()));
		menu.addSeparator();
		menu.addAction(tr("Save Value"), this, SLOT(saveOption()))->setEnabled(index.sibling(index.row(), 0).data(IsModifiedRole).toBool());
		menu.addAction(tr("Restore Default Value"), this, SLOT(resetOption()))->setEnabled(index.sibling(index.row(), 2).data(Qt::EditRole) != SettingsManager::getOptionDefinition(index.sibling(index.row(), 2).data(IdentifierRole).toInt()).defaultValue);
		menu.addSeparator();
	}

	menu.addAction(tr("Expand All"), m_ui->configurationViewWidget, SLOT(expandAll()));
	menu.addAction(tr("Collapse All"), m_ui->configurationViewWidget, SLOT(collapseAll()));
	menu.exec(m_ui->configurationViewWidget->mapToGlobal(position));
}

void ConfigurationContentsWidget::updateActions()
{
	const QModelIndex index(m_ui->configurationViewWidget->selectionModel()->hasSelection() ? m_ui->configurationViewWidget->currentIndex().sibling(m_ui->configurationViewWidget->currentIndex().row(), 2) : QModelIndex());
	const int identifier(index.data(IdentifierRole).toInt());

	if (identifier >= 0 && index.parent().isValid())
	{
		m_ui->nameLabelWidget->setText(SettingsManager::getOptionName(identifier));
		m_ui->currentValueLabelWidget->setText(SettingsManager::createDisplayValue(identifier, SettingsManager::getOption(identifier)));
		m_ui->defaultValueLabelWidget->setText(SettingsManager::createDisplayValue(identifier, SettingsManager::getOptionDefinition(identifier).defaultValue));
	}
	else
	{
		m_ui->nameLabelWidget->clear();
		m_ui->currentValueLabelWidget->clear();
		m_ui->defaultValueLabelWidget->clear();
	}
}

QString ConfigurationContentsWidget::getTitle() const
{
	return tr("Advanced Configuration");
}

QLatin1String ConfigurationContentsWidget::getType() const
{
	return QLatin1String("config");
}

QUrl ConfigurationContentsWidget::getUrl() const
{
	return QUrl(QLatin1String("about:config"));
}

QIcon ConfigurationContentsWidget::getIcon() const
{
	return ThemesManager::createIcon(QLatin1String("configuration"), false);
}

bool ConfigurationContentsWidget::eventFilter(QObject *object, QEvent *event)
{
	if (object == m_ui->configurationViewWidget && event->type() == QEvent::KeyPress)
	{
		const QKeyEvent *keyEvent(static_cast<QKeyEvent*>(event));
		const QModelIndex index(m_ui->configurationViewWidget->currentIndex());

		if (keyEvent && keyEvent->key() == Qt::Key_Right && index.parent().isValid())
		{
			m_ui->configurationViewWidget->setCurrentIndex(index.sibling(index.row(), 2));
		}
	}
	else if (object == m_ui->filterLineEditWidget && event->type() == QEvent::KeyPress)
	{
		const QKeyEvent *keyEvent(static_cast<QKeyEvent*>(event));

		if (keyEvent && keyEvent->key() == Qt::Key_Escape)
		{
			m_ui->filterLineEditWidget->clear();
		}
	}

	return ContentsWidget::eventFilter(object, event);
}

}
