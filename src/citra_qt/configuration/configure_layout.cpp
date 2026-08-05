// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QCheckBox>
#include <QColorDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QtGlobal>
#include "citra_qt/configuration/configuration_shared.h"
#include "citra_qt/configuration/configure_layout.h"
#include "citra_qt/configuration/configure_layout_cycle.h"
#include "common/settings.h"
#include "ui_configure_layout.h"
#ifdef ENABLE_OPENGL
#include "video_core/renderer_opengl/post_processing_opengl.h"
#endif

ConfigureLayout::ConfigureLayout(bool is_powered_on_, QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureLayout>()), is_powered_on{is_powered_on_} {
    ui->setupUi(this);

    // unison_group is inserted as its own top-level item in
    // ui->verticalLayout (scrollAreaWidgetContents' layout), right after
    // ui->layout_group (the "Screens" box the Layout dropdown itself lives
    // in) -- as close to "directly under the dropdown" as it can be while
    // still being a widget SetUnisonBlocked() can leave enabled: Qt ties a
    // widget's effective enabled state to its actual parent widget, which
    // addWidget()/insertWidget() sets to whatever widget owns the target
    // layout, not to how the layout is nested visually. Being a *sibling* of
    // layout_group (rather than nested inside its own verticalLayout_3) is
    // what makes it possible to disable layout_group -- and the tab's other
    // two top-level groups -- without disabling this too.
    auto* unison_group = new QGroupBox(tr("Unison Bildschirm-Streaming"), this);
    auto* unison_layout = new QGridLayout(unison_group);
    unison_checkbox = new QCheckBox(tr("Enable bottom screen streaming (Unison)"), unison_group);
    unison_port_spinbox = new QSpinBox(unison_group);
    unison_port_spinbox->setMaximum(65535);
    unison_layout->addWidget(unison_checkbox, 0, 0);
    unison_layout->addWidget(new QLabel(tr("Port:"), unison_group), 0, 1);
    unison_layout->addWidget(unison_port_spinbox, 0, 2);
    unison_layout->setColumnStretch(0, 1);
    ui->verticalLayout->insertWidget(1, unison_group);

    unison_checkbox->setEnabled(!is_powered_on);
    unison_port_spinbox->setEnabled(!is_powered_on);
    connect(unison_checkbox, &QCheckBox::clicked, this, [this](bool checked) {
        if (checked) {
            // The remote client now owns the bottom screen's touch input
            // (core/hle/service/hid/hid.cpp) -- showing it locally too is
            // redundant, so switch the local window to top-screen-only.
            // Unswapping is required, not optional: Single Screen shows
            // whichever screen swap_screen currently points at, and leaving
            // it swapped would show the bottom screen instead of the top.
            ui->layout_combobox->setCurrentIndex(
                static_cast<int>(Settings::LayoutOption::SingleScreen));
            ui->toggle_swap_screen->setChecked(false);
        }
        emit UnisonStreamingToggled(checked);
    });

    SetupPerGameUI();
    SetConfiguration();

    ui->large_screen_proportion->setEnabled(
        (Settings::values.layout_option.GetValue() == Settings::LayoutOption::LargeScreen));
    connect(ui->layout_combobox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](int currentIndex) {
                ui->large_screen_proportion->setEnabled(
                    currentIndex == (uint)(Settings::LayoutOption::LargeScreen));
            });

    ui->small_screen_position_combobox->setEnabled(
        (Settings::values.layout_option.GetValue() == Settings::LayoutOption::LargeScreen));
    connect(ui->layout_combobox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](int currentIndex) {
                ui->small_screen_position_combobox->setEnabled(
                    currentIndex == (uint)(Settings::LayoutOption::LargeScreen));
            });

    ui->single_screen_layout_config_group->setEnabled(
        (Settings::values.layout_option.GetValue() == Settings::LayoutOption::SingleScreen) ||
        (Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows));
    connect(ui->layout_combobox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](int currentIndex) {
                ui->single_screen_layout_config_group->setEnabled(
                    (currentIndex == (uint)(Settings::LayoutOption::SingleScreen)) ||
                    (currentIndex == (uint)(Settings::LayoutOption::SeparateWindows)));
            });

    ui->custom_layout_group->setEnabled(
        (Settings::values.layout_option.GetValue() == Settings::LayoutOption::CustomLayout));
    connect(ui->layout_combobox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](int currentIndex) {
                ui->custom_layout_group->setEnabled(currentIndex ==
                                                    (uint)(Settings::LayoutOption::CustomLayout));
            });

    ui->screen_top_leftright_padding->setEnabled(Settings::values.screen_top_stretch.GetValue());

#if QT_VERSION < QT_VERSION_CHECK(6, 7, 0)
    connect(ui->screen_top_stretch, static_cast<void (QCheckBox::*)(int)>(&QCheckBox::stateChanged),
            this,
            [this](bool checkState) { ui->screen_top_leftright_padding->setEnabled(checkState); });
    ui->screen_top_topbottom_padding->setEnabled(Settings::values.screen_top_stretch.GetValue());
    connect(ui->screen_top_stretch, static_cast<void (QCheckBox::*)(int)>(&QCheckBox::stateChanged),
            this,
            [this](bool checkState) { ui->screen_top_topbottom_padding->setEnabled(checkState); });
    ui->screen_bottom_leftright_padding->setEnabled(
        Settings::values.screen_bottom_topbottom_padding.GetValue());
    connect(
        ui->screen_bottom_stretch, static_cast<void (QCheckBox::*)(int)>(&QCheckBox::stateChanged),
        this,
        [this](bool checkState) { ui->screen_bottom_leftright_padding->setEnabled(checkState); });
    ui->screen_bottom_topbottom_padding->setEnabled(
        Settings::values.screen_bottom_topbottom_padding.GetValue());
    connect(
        ui->screen_bottom_stretch, static_cast<void (QCheckBox::*)(int)>(&QCheckBox::stateChanged),
        this,
        [this](bool checkState) { ui->screen_bottom_topbottom_padding->setEnabled(checkState); });
#else
    connect(ui->screen_top_stretch, &QCheckBox::checkStateChanged, this,
            [this](bool checkState) { ui->screen_top_leftright_padding->setEnabled(checkState); });
    ui->screen_top_topbottom_padding->setEnabled(Settings::values.screen_top_stretch.GetValue());
    connect(ui->screen_top_stretch, &QCheckBox::checkStateChanged, this,
            [this](bool checkState) { ui->screen_top_topbottom_padding->setEnabled(checkState); });
    ui->screen_bottom_leftright_padding->setEnabled(
        Settings::values.screen_bottom_topbottom_padding.GetValue());
    connect(
        ui->screen_bottom_stretch, &QCheckBox::checkStateChanged, this,
        [this](bool checkState) { ui->screen_bottom_leftright_padding->setEnabled(checkState); });
    ui->screen_bottom_topbottom_padding->setEnabled(
        Settings::values.screen_bottom_topbottom_padding.GetValue());
    connect(
        ui->screen_bottom_stretch, &QCheckBox::checkStateChanged, this,
        [this](bool checkState) { ui->screen_bottom_topbottom_padding->setEnabled(checkState); });
#endif

    connect(ui->bg_button, &QPushButton::clicked, this, [this] {
        ui->bg_button->setEnabled(false);
        const QColor new_bg_color = QColorDialog::getColor(bg_color);
        if (!new_bg_color.isValid()) {
            ui->bg_button->setEnabled(true);
            return;
        }
        bg_color = new_bg_color;
        QPixmap pixmap(ui->bg_button->size());
        pixmap.fill(bg_color);
        const QIcon color_icon(pixmap);
        ui->bg_button->setIcon(color_icon);
        ui->bg_button->setEnabled(true);
    });

    connect(ui->customize_layouts_to_cycle, &QPushButton::clicked, this, [this] {
        ui->customize_layouts_to_cycle->setEnabled(false);
        QDialog* layout_cycle_dialog = new ConfigureLayoutCycle(this);
        layout_cycle_dialog->exec();
        ui->customize_layouts_to_cycle->setEnabled(true);
    });
}

ConfigureLayout::~ConfigureLayout() = default;

void ConfigureLayout::SetConfiguration() {

    if (!Settings::IsConfiguringGlobal()) {
        ConfigurationShared::SetPerGameSetting(ui->layout_combobox,
                                               &Settings::values.layout_option);
    } else {
        ui->layout_combobox->setCurrentIndex(
            static_cast<int>(Settings::values.layout_option.GetValue()));
    }

    ui->toggle_swap_screen->setChecked(Settings::values.swap_screen.GetValue());
    ui->toggle_upright_screen->setChecked(Settings::values.upright_screen.GetValue());
    ui->screen_gap->setValue(Settings::values.screen_gap.GetValue());
    ui->large_screen_proportion->setValue(Settings::values.large_screen_proportion.GetValue());
    ui->small_screen_position_combobox->setCurrentIndex(
        static_cast<int>(Settings::values.small_screen_position.GetValue()));
    ui->custom_top_x->setValue(Settings::values.custom_top_x.GetValue());
    ui->custom_top_y->setValue(Settings::values.custom_top_y.GetValue());
    ui->custom_top_width->setValue(Settings::values.custom_top_width.GetValue());
    ui->custom_top_height->setValue(Settings::values.custom_top_height.GetValue());
    ui->custom_bottom_x->setValue(Settings::values.custom_bottom_x.GetValue());
    ui->custom_bottom_y->setValue(Settings::values.custom_bottom_y.GetValue());
    ui->custom_bottom_width->setValue(Settings::values.custom_bottom_width.GetValue());
    ui->custom_bottom_height->setValue(Settings::values.custom_bottom_height.GetValue());
    ui->custom_second_layer_opacity->setValue(
        Settings::values.custom_second_layer_opacity.GetValue());

    ui->screen_top_stretch->setChecked(Settings::values.screen_top_stretch.GetValue());
    ui->screen_top_leftright_padding->setValue(
        Settings::values.screen_top_leftright_padding.GetValue());
    ui->screen_top_topbottom_padding->setValue(
        Settings::values.screen_top_topbottom_padding.GetValue());
    ui->screen_bottom_stretch->setChecked(Settings::values.screen_bottom_stretch.GetValue());
    ui->screen_bottom_leftright_padding->setValue(
        Settings::values.screen_bottom_leftright_padding.GetValue());
    ui->screen_bottom_topbottom_padding->setValue(
        Settings::values.screen_bottom_topbottom_padding.GetValue());
    bg_color =
        QColor::fromRgbF(Settings::values.bg_red.GetValue(), Settings::values.bg_green.GetValue(),
                         Settings::values.bg_blue.GetValue());
    QPixmap pixmap(ui->bg_button->size());
    pixmap.fill(bg_color);
    const QIcon color_icon(pixmap);
    ui->bg_button->setIcon(color_icon);

    unison_checkbox->setChecked(Settings::values.enable_bottom_screen_streaming.GetValue());
    unison_port_spinbox->setValue(Settings::values.bottom_screen_streaming_port.GetValue());
}

void ConfigureLayout::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureLayout::ApplyConfiguration() {
    Settings::values.large_screen_proportion = ui->large_screen_proportion->value();
    Settings::values.screen_gap = ui->screen_gap->value();
    Settings::values.small_screen_position = static_cast<Settings::SmallScreenPosition>(
        ui->small_screen_position_combobox->currentIndex());
    Settings::values.custom_top_x = ui->custom_top_x->value();
    Settings::values.custom_top_y = ui->custom_top_y->value();
    Settings::values.custom_top_width = ui->custom_top_width->value();
    Settings::values.custom_top_height = ui->custom_top_height->value();
    Settings::values.custom_bottom_x = ui->custom_bottom_x->value();
    Settings::values.custom_bottom_y = ui->custom_bottom_y->value();
    Settings::values.custom_bottom_width = ui->custom_bottom_width->value();
    Settings::values.custom_bottom_height = ui->custom_bottom_height->value();
    Settings::values.custom_second_layer_opacity = ui->custom_second_layer_opacity->value();

    Settings::values.screen_top_stretch = ui->screen_top_stretch->checkState();
    Settings::values.screen_top_leftright_padding = ui->screen_top_leftright_padding->value();
    Settings::values.screen_top_topbottom_padding = ui->screen_top_topbottom_padding->value();
    Settings::values.screen_bottom_stretch = ui->screen_bottom_stretch->checkState();
    Settings::values.screen_bottom_leftright_padding = ui->screen_bottom_leftright_padding->value();
    Settings::values.screen_bottom_topbottom_padding = ui->screen_bottom_topbottom_padding->value();

    ConfigurationShared::ApplyPerGameSetting(&Settings::values.layout_option, ui->layout_combobox);
    ConfigurationShared::ApplyPerGameSetting(&Settings::values.swap_screen, ui->toggle_swap_screen,
                                             swap_screen);
    ConfigurationShared::ApplyPerGameSetting(&Settings::values.upright_screen,
                                             ui->toggle_upright_screen, upright_screen);

    Settings::values.bg_red = static_cast<float>(bg_color.redF());
    Settings::values.bg_green = static_cast<float>(bg_color.greenF());
    Settings::values.bg_blue = static_cast<float>(bg_color.blueF());

    Settings::values.enable_bottom_screen_streaming = unison_checkbox->isChecked();
    Settings::values.bottom_screen_streaming_port = static_cast<u16>(unison_port_spinbox->value());
}

void ConfigureLayout::SetupPerGameUI() {
    // Block the global settings if a game is currently running that overrides them
    if (Settings::IsConfiguringGlobal()) {
        ui->toggle_swap_screen->setEnabled(Settings::values.swap_screen.UsingGlobal());
        ui->toggle_upright_screen->setEnabled(Settings::values.upright_screen.UsingGlobal());
        return;
    }

    ui->bg_color_group->setVisible(false);

    ConfigurationShared::SetColoredTristate(ui->toggle_swap_screen, Settings::values.swap_screen,
                                            swap_screen);
    ConfigurationShared::SetColoredTristate(ui->toggle_upright_screen,
                                            Settings::values.upright_screen, upright_screen);

    ConfigurationShared::SetColoredComboBox(
        ui->layout_combobox, ui->widget_layout,
        static_cast<int>(Settings::values.layout_option.GetValue(true)));
}

void ConfigureLayout::SetUnisonBlocked(bool blocked) {
    // The tab's three top-level setting groups get disabled -- unison_group
    // (inserted as a sibling of these in the constructor) is deliberately
    // not among them, so the checkbox stays reachable to turn streaming
    // back off.
    ui->layout_group->setEnabled(!blocked);
    ui->custom_layout_group->setEnabled(!blocked);
    ui->single_screen_layout_config_group->setEnabled(!blocked);
}
