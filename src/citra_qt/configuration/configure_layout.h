// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QWidget>
#include "common/common_types.h"

namespace Settings {
enum class StereoRenderOption : u32;
}

namespace ConfigurationShared {
enum class CheckState;
}

class QCheckBox;
class QSpinBox;

namespace Ui {
class ConfigureLayout;
}

class ConfigureLayout : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureLayout(bool is_powered_on, QWidget* parent = nullptr);
    ~ConfigureLayout();

    void ApplyConfiguration();
    void RetranslateUI();
    void SetConfiguration();

    void SetupPerGameUI();

    // Screen layout has no meaningful effect on what a finlink bottom-screen
    // streaming client sees or controls (see core/streaming/
    // bottom_screen_stream.h) and changing it locally while a client is
    // connected would just be confusing, so this disables the tab's three
    // top-level setting groups (layout_group, custom_layout_group,
    // single_screen_layout_config_group) for as long as streaming is
    // enabled -- except the streaming controls themselves (finlink_group, a
    // sibling of those three, never touched by this), since the user still
    // needs a way to turn streaming back off from here.
    void SetFinlinkBlocked(bool blocked);

signals:
    // Emitted when the user clicks the checkbox itself (only possible while
    // !is_powered_on, see the constructor) -- ConfigureDialog uses this to
    // keep the Input tab's finlink-blocked state live instead of only
    // reflecting whatever was true when the dialog was opened.
    void FinlinkStreamingToggled(bool enabled);

private:
    void updateShaders(Settings::StereoRenderOption stereo_option);
    void updateTextureFilter(int index);

    std::unique_ptr<Ui::ConfigureLayout> ui;
    ConfigurationShared::CheckState swap_screen;
    ConfigurationShared::CheckState upright_screen;
    QColor bg_color;
    bool is_powered_on;

    // finlink bottom-screen streaming controls, inserted in the constructor
    // as a sibling of layout_group in ui->verticalLayout (right after it,
    // i.e. directly under the Screen Layout dropdown) -- deliberately not
    // nested inside any of the .ui file's own group boxes, so
    // SetFinlinkBlocked() can disable those without also disabling the one
    // control that turns streaming back off.
    QCheckBox* finlink_checkbox = nullptr;
    QSpinBox* finlink_port_spinbox = nullptr;
};
