#pragma once

// A reset is permitted only after opening the prompt, explicitly selecting
// Reset, and confirming while the prompt is visible. Every exit disarms it.
class PetResetConfirmation {
  enum class Choice { Closed, Cancel, Reset };
  Choice choice = Choice::Closed;

public:
  void open() { choice = Choice::Cancel; }
  void cancel() { choice = Choice::Closed; }
  bool resetSelected() const { return choice == Choice::Reset; }

  void toggle() {
    if (choice == Choice::Cancel) choice = Choice::Reset;
    else if (choice == Choice::Reset) choice = Choice::Cancel;
  }

  bool confirm(bool promptVisible) {
    const bool reset = promptVisible && resetSelected();
    cancel();
    return reset;
  }
};
