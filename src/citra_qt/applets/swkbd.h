// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QDialog>
#include <QValidator>
#include "core/frontend/applets/swkbd.h"

namespace Core {
class System;
}

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;
class QtKeyboard;

class QtKeyboardValidator final : public QValidator {
public:
    explicit QtKeyboardValidator(QtKeyboard* keyboard);
    State validate(QString& input, int& pos) const override;

private:
    QtKeyboard* keyboard;
};

class QtKeyboardDialog final : public QDialog {
    Q_OBJECT

public:
    QtKeyboardDialog(QWidget* parent, QtKeyboard* keyboard);
    void Submit();

private:
    void HandleValidationError(Frontend::ValidationError error);
    QDialogButtonBox* buttons;
    QLabel* label;
    QLineEdit* line_edit;
    QVBoxLayout* layout;
    QtKeyboard* keyboard;
    QString text;
    u8 button;

    friend class QtKeyboard;
};

class QtKeyboard final : public QObject, public Frontend::SoftwareKeyboard {
    Q_OBJECT

public:
    explicit QtKeyboard(QWidget& parent, Core::System& system_);
    void Execute(const Frontend::KeyboardConfig& config) override;
    void ShowError(const std::string& error) override;

private:
    Q_INVOKABLE void OpenInputDialog();
    Q_INVOKABLE void ShowErrorDialog(QString message);

    /// Index of the buttons
    u8 ok_id;
    static constexpr u8 forgot_id = 1;
    static constexpr u8 cancel_id = 0;

    QWidget& parent;
    // Used by Execute() to check for and forward to an actively streaming
    // Unison client (Core::Streaming::Server, N3DS_BOTTOM_SCREEN) instead
    // of showing this class's own local dialog -- see that call site's own
    // comment.
    Core::System& system;

    std::string result_text;
    int result_button;

    friend class QtKeyboardDialog;
    friend class QtKeyboardValidator;
};
