// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include <QListWidgetItem>
#include "citra_qt/configuration/configure_audio.h"
#include "citra_qt/configuration/configure_camera.h"
#include "citra_qt/configuration/configure_debug.h"
#include "citra_qt/configuration/configure_dialog.h"
#include "citra_qt/configuration/configure_enhancements.h"
#include "citra_qt/configuration/configure_general.h"
#include "citra_qt/configuration/configure_graphics.h"
#include "citra_qt/configuration/configure_hotkeys.h"
#include "citra_qt/configuration/configure_hotkeys_controller.h"
#include "citra_qt/configuration/configure_input.h"
#include "citra_qt/configuration/configure_layout.h"
#include "citra_qt/configuration/configure_network.h"
#include "citra_qt/configuration/configure_storage.h"
#include "citra_qt/configuration/configure_system.h"
#include "citra_qt/configuration/configure_ui.h"
#include "citra_qt/hotkeys.h"
#include "common/settings.h"
#include "core/core.h"
#include "ui_configure.h"

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_, Core::System& system_,
                                 QString gl_renderer, std::span<const QString> physical_devices,
                                 bool enable_web_config)
    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()}, registry{registry_},
      system{system_}, is_powered_on{system.IsPoweredOn()},
      general_tab{std::make_unique<ConfigureGeneral>(this)},
      system_tab{std::make_unique<ConfigureSystem>(system, this)},
      input_tab{std::make_unique<ConfigureInput>(system, this)},
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(this)},
      hotkeys_controller_tab{std::make_unique<ConfigureControllerHotkeys>(this)},
      graphics_tab{
          std::make_unique<ConfigureGraphics>(gl_renderer, physical_devices, is_powered_on, this)},
      enhancements_tab{std::make_unique<ConfigureEnhancements>(this)},
      layout_tab{std::make_unique<ConfigureLayout>(is_powered_on, this)},
      audio_tab{std::make_unique<ConfigureAudio>(is_powered_on, this)},
      camera_tab{std::make_unique<ConfigureCamera>(this)},
      debug_tab{std::make_unique<ConfigureDebug>(is_powered_on, this)},
      storage_tab{std::make_unique<ConfigureStorage>(is_powered_on, this)},
      web_tab{std::make_unique<ConfigureWeb>(this)}, ui_tab{std::make_unique<ConfigureUi>(this)},
      finlink_streaming_blocked{Settings::values.enable_bottom_screen_streaming.GetValue()} {
    Settings::SetConfiguringGlobal(true);

    ui->setupUi(this);

    layout_tab->SetFinlinkBlocked(finlink_streaming_blocked);
    input_tab->SetFinlinkBlocked(finlink_streaming_blocked);
    connect(layout_tab.get(), &ConfigureLayout::FinlinkStreamingToggled, this,
            &ConfigureDialog::OnFinlinkStreamingToggled);

    ui->tabWidget->addTab(general_tab.get(), tr("General"));
    ui->tabWidget->addTab(system_tab.get(), tr("System"));
    ui->tabWidget->addTab(input_tab.get(), tr("Input"));
    ui->tabWidget->addTab(hotkeys_controller_tab.get(), tr("Controller Hotkeys"));
    ui->tabWidget->addTab(hotkeys_tab.get(), tr("Keyboard Hotkeys"));
    ui->tabWidget->addTab(graphics_tab.get(), tr("Graphics"));
    ui->tabWidget->addTab(enhancements_tab.get(), tr("Enhancements"));
    ui->tabWidget->addTab(layout_tab.get(), tr("Layout"));
    ui->tabWidget->addTab(audio_tab.get(), tr("Audio"));
    ui->tabWidget->addTab(camera_tab.get(), tr("Camera"));
    ui->tabWidget->addTab(debug_tab.get(), tr("Debug"));
    ui->tabWidget->addTab(storage_tab.get(), tr("Storage"));
    ui->tabWidget->addTab(web_tab.get(), tr("Network"));
    ui->tabWidget->addTab(ui_tab.get(), tr("UI"));

    hotkeys_tab->Populate(registry);
    hotkeys_controller_tab->Populate(registry);
    PopulateSelectionList();

    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);
    connect(ui->selectorList, &QListWidget::itemSelectionChanged, this,
            &ConfigureDialog::UpdateVisibleTabs);

    adjustSize();
    ui->selectorList->setCurrentRow(0);

    // Set up used key list synchronisation
    connect(input_tab.get(), &ConfigureInput::InputKeysChanged, hotkeys_tab.get(),
            &ConfigureHotkeys::OnInputKeysChanged);
    connect(hotkeys_tab.get(), &ConfigureHotkeys::HotkeysChanged, input_tab.get(),
            &ConfigureInput::OnHotkeysChanged);

    // Synchronise lists upon initialisation
    input_tab->EmitInputKeysChanged();
    hotkeys_tab->EmitHotkeysChanged();
}

ConfigureDialog::~ConfigureDialog() = default;

void ConfigureDialog::SetConfiguration() {
    general_tab->SetConfiguration();
    system_tab->SetConfiguration();
    input_tab->LoadConfiguration();
    graphics_tab->SetConfiguration();
    enhancements_tab->SetConfiguration();
    layout_tab->SetConfiguration();
    audio_tab->SetConfiguration();
    camera_tab->SetConfiguration();
    debug_tab->SetConfiguration();
    web_tab->SetConfiguration();
    ui_tab->SetConfiguration();
    storage_tab->SetConfiguration();
}

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    input_tab->ApplyProfile();
    hotkeys_tab->ApplyConfiguration(registry);
    hotkeys_controller_tab->ApplyConfiguration(registry);
    graphics_tab->ApplyConfiguration();
    enhancements_tab->ApplyConfiguration();
    layout_tab->ApplyConfiguration();
    audio_tab->ApplyConfiguration();
    camera_tab->ApplyConfiguration();
    debug_tab->ApplyConfiguration();
    web_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    storage_tab->ApplyConfiguration();
    system.ApplySettings();
    Settings::LogSettings();
}

Q_DECLARE_METATYPE(QList<QWidget*>);

void ConfigureDialog::PopulateSelectionList() {
    ui->selectorList->clear();

    const std::array<std::pair<QString, QList<QWidget*>>, 5> items{
        {{tr("General"), {general_tab.get(), web_tab.get(), debug_tab.get(), ui_tab.get()}},
         {tr("System"), {system_tab.get(), camera_tab.get(), storage_tab.get()}},
         {tr("Graphics"), {enhancements_tab.get(), layout_tab.get(), graphics_tab.get()}},
         {tr("Audio"), {audio_tab.get()}},
         {tr("Controls"), {input_tab.get(), hotkeys_controller_tab.get(), hotkeys_tab.get()}}}};

    for (const auto& entry : items) {
        auto* const item = new QListWidgetItem(entry.first);
        item->setData(Qt::UserRole, QVariant::fromValue(entry.second));

        ui->selectorList->addItem(item);
    }
}

void ConfigureDialog::OnLanguageChanged(const QString& locale) {
    emit LanguageChanged(locale);
    // first apply the configuration, and then restore the display
    ApplyConfiguration();
    RetranslateUI();
    SetConfiguration();
}

void ConfigureDialog::RetranslateUI() {
    int old_row = ui->selectorList->currentRow();
    int old_index = ui->tabWidget->currentIndex();
    ui->retranslateUi(this);
    PopulateSelectionList();
    // restore selection after repopulating
    ui->selectorList->setCurrentRow(old_row);
    ui->tabWidget->setCurrentIndex(old_index);

    general_tab->RetranslateUI();
    system_tab->RetranslateUI();
    input_tab->RetranslateUI();
    hotkeys_tab->RetranslateUI();
    hotkeys_controller_tab->RetranslateUI();
    graphics_tab->RetranslateUI();
    enhancements_tab->RetranslateUI();
    layout_tab->RetranslateUI();
    audio_tab->RetranslateUI();
    camera_tab->RetranslateUI();
    debug_tab->RetranslateUI();
    web_tab->RetranslateUI();
    ui_tab->RetranslateUI();
    storage_tab->RetranslateUI();
}

void ConfigureDialog::UpdateVisibleTabs() {
    const auto items = ui->selectorList->selectedItems();
    if (items.isEmpty())
        return;

    const std::map<QWidget*, QString> widgets = {
        {general_tab.get(), tr("General")},
        {system_tab.get(), tr("System")},
        {input_tab.get(), tr("Input")},
        {hotkeys_tab.get(), tr("Keyboard Hotkeys")},
        {hotkeys_controller_tab.get(), tr("Controller Hotkeys")},
        {enhancements_tab.get(), tr("Enhancements")},
        {layout_tab.get(), tr("Layout")},
        {graphics_tab.get(), tr("Advanced")},
        {audio_tab.get(), tr("Audio")},
        {camera_tab.get(), tr("Camera")},
        {debug_tab.get(), tr("Debug")},
        {storage_tab.get(), tr("Storage")},
        {web_tab.get(), tr("Network")},
        {ui_tab.get(), tr("UI")}};

    ui->tabWidget->clear();

    const QList<QWidget*> tabs = qvariant_cast<QList<QWidget*>>(items[0]->data(Qt::UserRole));

    for (const auto tab : tabs) {
        const int index = ui->tabWidget->addTab(tab, TabTitle(tab, widgets.at(tab)));
        if (finlink_streaming_blocked && tab == input_tab.get()) {
            ui->tabWidget->setTabEnabled(index, false);
        }
    }
}

QString ConfigureDialog::TabTitle(QWidget* tab, const QString& base) const {
    // layout_tab is deliberately not covered here -- it stays selectable and
    // labeled normally even while streaming is on; ConfigureLayout disables
    // everything on the page except its own streaming controls instead (see
    // ConfigureLayout::SetFinlinkBlocked). Only input_tab still gets the
    // whole-tab treatment, since none of it is reachable from within itself
    // the way turning streaming off is from within the Layout tab.
    if (finlink_streaming_blocked && tab == input_tab.get()) {
        return base + tr(" (durch finlink blockiert)");
    }
    return base;
}

void ConfigureDialog::OnFinlinkStreamingToggled(bool enabled) {
    finlink_streaming_blocked = enabled;
    layout_tab->SetFinlinkBlocked(enabled);
    input_tab->SetFinlinkBlocked(enabled);
    // Patches the Input tab's entry directly instead of going through
    // UpdateVisibleTabs()'s full clear()+rebuild: this slot fires while the
    // user is sitting in the Layout tab (Graphics category), so Input tab
    // usually isn't even in ui->tabWidget right now -- and rebuilding
    // unrelated tab state reactively here was implicated in a previous bug
    // where toggling this checkbox bounced the dialog's whole category
    // selection back to General. UpdateVisibleTabs() still recomputes this
    // correctly and safely on its own the next time the user switches to
    // the Controls category, since it reads finlink_streaming_blocked fresh.
    for (int i = 0; i < ui->tabWidget->count(); i++) {
        if (ui->tabWidget->widget(i) == input_tab.get()) {
            ui->tabWidget->setTabText(i, TabTitle(input_tab.get(), tr("Input")));
            ui->tabWidget->setTabEnabled(i, !enabled);
            break;
        }
    }
}
