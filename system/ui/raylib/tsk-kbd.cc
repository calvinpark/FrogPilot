#include "system/ui/raylib/util.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Constants & Utility
// -----------------------------------------------------------------------------
static const int   FONT_SIZE              = 100;
static const int   ERROR_LABEL_FONT_SIZE  = 80;
static const float FONT_SPACING           = 1.0f;
static const float INPUT_FONT_SPACING     = 3.0f;

static const int   INPUT_BOX_PADDING      = 20;
static const int   CHARS_LEFT_LABEL_SPACE = 20;

static const int   KEY_HEIGHT             = 180;
static const int   KEY_PADDING            = 10;
static const int   NUM_KEYS_FIRST_ROW     = 10;
static const int   NUM_KEYS_SECOND_ROW    = 7;
static const int   INPUT_BOX_CHARS        = 32;

inline bool TappedInside(const Rectangle &rect) {
  return CheckCollisionPointRec(GetMousePosition(), rect)
         && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Writes content to a file, returning (success, errorMessages).
std::pair<bool, std::vector<std::string>> writeToFileWithError(
  const std::string &path, const std::string &content
) {
  std::ofstream file(path, std::ios::out | std::ios::trunc);
  std::vector<std::string> errors;

  if (!file.is_open()) {
    errors.push_back("Failed to open file '" + path + "'");
    errors.push_back("Error: " + std::string(strerror(errno)));
    return {false, errors};
  }

  file << content;
  file.close();
  if (!file.good()) {
    errors.push_back("Failed to write to file '" + path + "'");
    errors.push_back("Error: " + std::string(strerror(errno)));
    return {false, errors};
  }
  return {true, {}};
}

// Reads a valid 32-digit lowercase hex from file, or returns empty if invalid.
std::string readAndValidateKeyFile(const std::string &filePath) {
  std::ifstream file(filePath);
  if (!file.is_open()) return "";
  std::string content;
  file >> content;
  return (std::regex_match(content, std::regex("^[a-f0-9]{32}$"))) ? content : "";
}

// Reads SecOCKey: returns "Installed: <key>" or "Installed: Invalid(...)" or "Installed: None".
std::string readSecOCKey() {
  const std::string filePath = "/data/params/d/SecOCKey";
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return "Installed: None";
  }

  auto size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<char> buffer(size);
  if (!file.read(buffer.data(), size)) {
    return "Installed: None";
  }

  // Check for non-printable characters
  bool hasInvalidChars = std::any_of(
    buffer.begin(), buffer.end(),
    [](char c){ return (c < 32 && c != '\n' && c != '\r'); }
  );
  if (hasInvalidChars) {
    return "Installed: Invalid (binary file)";
  }

  // Remove all newlines and validate 32-char hex
  std::string content(buffer.begin(), buffer.end());
  content.erase(std::remove(content.begin(), content.end(), '\n'), content.end());

  if (std::regex_match(content, std::regex("^[a-f0-9]{32}$"))) {
    return "Installed: " + content;
  }
  return "Installed: Invalid (" + content + ")";
}

// -----------------------------------------------------------------------------
// Encapsulates the entire keyboard UI, including layout, state, logic,
// and now includes the "Installed" label refresh & draw.
// -----------------------------------------------------------------------------
class KeyboardUI {
private:
  // Fonts
  Font regularFont;
  Font inputFont;

  // Keyboard state
  std::string inputText;
  bool showCharsLeftLabel = true;
  bool showInstallButton  = false;
  bool showSuccessLabel   = false;
  std::vector<std::string> errorLines;

  // "Installed" label, updated once per second
  std::string installedLabel = "Installed: None";
  std::chrono::steady_clock::time_point lastInstalledCheck;

  // Geometry / Layout
  Rectangle hideRect;     // "Hide" button rectangle
  Rectangle inputBoxRect; // Input box rectangle
  Rectangle installRect;  // "Install this key" button rectangle

  std::vector<Rectangle> keyRects;
  std::vector<std::string> keyTexts = {
    "1","2","3","4","5","6","7","8","9","0",
    "a","b","c","d","e","f","<"
  };

public:
  KeyboardUI() {
    initFonts();
    initLayout();
    loadInitialKey();
    lastInstalledCheck = std::chrono::steady_clock::now();
  }

  ~KeyboardUI() {
    UnloadFont(regularFont);
    UnloadFont(inputFont);
  }

  // Called every frame to handle taps, update states
  void update() {
    // A) Update "Installed" label every ~1 second
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastInstalledCheck).count() >= 1) {
      installedLabel = readSecOCKey();
      lastInstalledCheck = now;
    }

    // B) Check on-screen keyboard taps
    for (size_t i = 0; i < keyRects.size(); ++i) {
      if (TappedInside(keyRects[i])) {
        handleKeyTap(i);
        break;
      }
    }

    // C) Decide if "Install" button is visible
    showInstallButton = (inputText.size() == INPUT_BOX_CHARS)
                        && errorLines.empty()
                        && !showSuccessLabel;

    // D) Recompute installRect if needed
    if (showInstallButton) {
      Vector2 installSize = MeasureTextEx(regularFont, "Install this key",
                                          FONT_SIZE, FONT_SPACING);
      installRect = {
        (GetScreenWidth() - installSize.x - 40) / 2.0f,
        inputBoxRect.y + inputBoxRect.height + CHARS_LEFT_LABEL_SPACE,
        installSize.x + 40,
        installSize.y + 20
      };
    }

    // E) If user taps "Install" button
    if (showInstallButton && TappedInside(installRect)) {
      installKey();
    }
  }

  // Called every frame to draw the UI
  void draw() {
    drawHideButton();
    drawInstalledLabel(); // <--- Draw the "Installed" label here
    drawInputArea();
    drawKeyboard();
    drawStatusMessages();
  }

  // For main() to check if user tapped "Hide" so we can exit
  bool tappedHide() const {
    return TappedInside(hideRect);
  }

private:
  // ---------------------------------
  // Initialization / layout
  // ---------------------------------
  void initFonts() {
    regularFont = LoadFontEx("/data/openpilot/selfdrive/assets/fonts/Inter-Regular.ttf",
                             FONT_SIZE * 2, nullptr, 0);
    SetTextureFilter(regularFont.texture, TEXTURE_FILTER_ANISOTROPIC_4X);

    inputFont = LoadFontEx("/data/openpilot/selfdrive/assets/fonts/Inter-Bold.ttf",
                           FONT_SIZE * 2, nullptr, 0);
    SetTextureFilter(inputFont.texture, TEXTURE_FILTER_ANISOTROPIC_4X);
  }

  void initLayout() {
    // A local constant for the hide button's padding
    const float exitPadding = 20.0f;
    // Hide button
    const std::string hideText = "Hide";
    Vector2 hideSize = MeasureTextEx(regularFont, hideText.c_str(),
                                     FONT_SIZE, FONT_SPACING);
    hideRect = {
      static_cast<float>(GetScreenWidth() - hideSize.x - exitPadding),
      exitPadding / 2.0f,
      hideSize.x + exitPadding,
      hideSize.y + exitPadding
    };

    // Input box geometry
    float measureRef = MeasureTextEx(
      inputFont, "00000000000000000000000000000000",
      FONT_SIZE, INPUT_FONT_SPACING
    ).x;
    int inputBoxWidth  = static_cast<int>(measureRef);
    int inputBoxHeight = FONT_SIZE + INPUT_BOX_PADDING * 2;

    int screenWidth  = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    int keyboardY    = screenHeight - KEY_HEIGHT * 2 - KEY_PADDING * 3;

    inputBoxRect = {
      (screenWidth - inputBoxWidth) / 2.0f,
      (keyboardY / 2.0f) - (inputBoxHeight / 2.0f),
      static_cast<float>(inputBoxWidth),
      static_cast<float>(inputBoxHeight)
    };

    // Keyboard key rectangles
    keyRects.reserve(keyTexts.size());
    for (size_t i = 0; i < keyTexts.size(); ++i) {
      int row = (i < NUM_KEYS_FIRST_ROW) ? 0 : 1;
      int col = (row == 0) ? (int)i : (int)(i - NUM_KEYS_FIRST_ROW);

      int keyWidth = (row == 0)
        ? (screenWidth - (NUM_KEYS_FIRST_ROW + 1) * KEY_PADDING) / NUM_KEYS_FIRST_ROW
        : (screenWidth - (NUM_KEYS_SECOND_ROW + 1) * KEY_PADDING) / NUM_KEYS_SECOND_ROW;

      keyRects.push_back({
        static_cast<float>(KEY_PADDING + (keyWidth + KEY_PADDING) * col),
        static_cast<float>(keyboardY + (KEY_HEIGHT + KEY_PADDING) * row),
        static_cast<float>(keyWidth),
        static_cast<float>(KEY_HEIGHT)
      });
    }
  }

  void loadInitialKey() {
    std::string persistKey = readAndValidateKeyFile("/persist/tsk/key");
    std::string secOCKey   = readAndValidateKeyFile("/data/params/d/SecOCKey");
    if (!secOCKey.empty()) {
      inputText = secOCKey;
    } else if (!persistKey.empty()) {
      inputText = persistKey;
    } else {
      inputText.clear();
    }
    showCharsLeftLabel = (inputText.size() < INPUT_BOX_CHARS);
  }

  // ---------------------------------
  // Event Handlers
  // ---------------------------------
  void handleKeyTap(size_t index) {
    if (keyTexts[index] == "<") {
      // Backspace
      if (!inputText.empty()) {
        inputText.pop_back();
        showInstallButton  = false;
        showSuccessLabel   = false;
        errorLines.clear();
        showCharsLeftLabel = (inputText.size() < INPUT_BOX_CHARS);
      }
    } else if (inputText.size() < INPUT_BOX_CHARS) {
      // Append character
      inputText += keyTexts[index];
      showCharsLeftLabel = (inputText.size() < INPUT_BOX_CHARS);
    }
  }

  void installKey() {
    errorLines.clear();
    auto [success, errors] = writeToFileWithError("/data/params/d/SecOCKey", inputText);
    if (success) {
      showSuccessLabel = true;
    } else {
      errorLines.insert(errorLines.end(), errors.begin(), errors.end());
      showSuccessLabel = false;
    }
    showInstallButton = false;
  }

  // ---------------------------------
  // Drawing Helpers
  // ---------------------------------

  void drawHideButton() {
    DrawRectangleRec(hideRect, RAYLIB_GRAY);
    DrawTextEx(
      regularFont, "Hide",
      {hideRect.x + 10, hideRect.y + 10},
      FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
    );
  }

  // **NEW**: Draw the "Installed" label above the input box
  void drawInstalledLabel() {
    // We'll use e.g. 80 for font size, or you can reuse your definitions
    constexpr int INSTALLED_LABEL_FONT_SIZE = 80;
    Vector2 size = MeasureTextEx(
      regularFont, installedLabel.c_str(),
      INSTALLED_LABEL_FONT_SIZE, FONT_SPACING
    );
    float x = (GetScreenWidth() - size.x) / 2.0f;
    float y = inputBoxRect.y - size.y - 20.0f; // 20 is the gap above the input box

    DrawTextEx(
      regularFont, installedLabel.c_str(),
      {x, y},
      INSTALLED_LABEL_FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
    );
  }

  void drawInputArea() {
    DrawRectangleRec(inputBoxRect, RAYLIB_BLACK);
    DrawRectangleLinesEx(inputBoxRect, 2, RAYLIB_RAYWHITE);

    float textX        = inputBoxRect.x + INPUT_BOX_PADDING;
    float textY        = inputBoxRect.y + INPUT_BOX_PADDING;
    float groupSpacing = MeasureTextEx(inputFont, " ", 30, INPUT_FONT_SPACING).x;

    std::vector<std::string> DARK_COLORS = {
      "#6A0DAD","#2F4F4F","#556B2F","#8B0000","#1874CD","#006400"
    };
    std::vector<Color> colors;
    colors.reserve(DARK_COLORS.size());
    for (auto &hex : DARK_COLORS) {
      unsigned int r, g, b;
      if (sscanf(hex.c_str(), "#%02x%02x%02x", &r, &g, &b) == 3) {
        colors.push_back(Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, 255});
      } else {
        colors.push_back(RAYLIB_GRAY);
      }
    }

    for (int i = 0; i < (int)inputText.size(); i += 4) {
      std::string group = inputText.substr(i, 4);
      Color color       = colors[(i / 4) % (int)colors.size()];
      DrawTextEx(inputFont, group.c_str(), {textX, textY},
                 FONT_SIZE, INPUT_FONT_SPACING, color);

      float groupW = MeasureTextEx(inputFont, group.c_str(),
                                   FONT_SIZE, INPUT_FONT_SPACING).x;
      textX += groupW + groupSpacing;
    }

    // Characters left label
    if (showCharsLeftLabel) {
      int charsLeft = INPUT_BOX_CHARS - (int)inputText.size();
      std::string leftStr = std::to_string(charsLeft) + " characters left";
      Vector2 leftSize    = MeasureTextEx(
        regularFont, leftStr.c_str(), FONT_SIZE, FONT_SPACING
      );
      DrawTextEx(
        regularFont, leftStr.c_str(),
        {
          (GetScreenWidth() - leftSize.x) / 2.0f,
          inputBoxRect.y + inputBoxRect.height + CHARS_LEFT_LABEL_SPACE
        },
        FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
      );
    }
  }

  void drawKeyboard() {
    for (size_t i = 0; i < keyRects.size(); ++i) {
      DrawRectangleRec(keyRects[i], RAYLIB_GRAY);
      Vector2 keySize = MeasureTextEx(
        regularFont, keyTexts[i].c_str(), FONT_SIZE, FONT_SPACING
      );
      DrawTextEx(
        regularFont, keyTexts[i].c_str(),
        {
          keyRects[i].x + (keyRects[i].width - keySize.x) / 2.0f,
          keyRects[i].y + (keyRects[i].height - keySize.y) / 2.0f
        },
        FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
      );
    }
  }

  void drawStatusMessages() {
    // "Install this key" button (if needed)
    if (showInstallButton) {
      DrawRectangleRec(installRect, RAYLIB_GRAY);
      DrawTextEx(
        regularFont, "Install this key",
        {installRect.x + 20, installRect.y + 10},
        FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
      );
    }

    // Success label
    if (showSuccessLabel) {
      const std::string successText = "Success!";
      Vector2 successSize = MeasureTextEx(
        regularFont, successText.c_str(), FONT_SIZE, FONT_SPACING
      );
      DrawTextEx(
        regularFont, successText.c_str(),
        {
          installRect.x + (installRect.width - successSize.x) / 2.0f,
          installRect.y + (installRect.height - successSize.y) / 2.0f
        },
        FONT_SIZE, FONT_SPACING, RAYLIB_GREEN
      );
    }

    // Error messages
    if (!errorLines.empty()) {
      float errY = showInstallButton
        ? installRect.y + installRect.height + CHARS_LEFT_LABEL_SPACE
        : inputBoxRect.y + inputBoxRect.height + CHARS_LEFT_LABEL_SPACE;

      for (auto &line : errorLines) {
        Vector2 errSize = MeasureTextEx(
          regularFont, line.c_str(), ERROR_LABEL_FONT_SIZE, FONT_SPACING
        );
        DrawTextEx(
          regularFont, line.c_str(),
          {(GetScreenWidth() - errSize.x) / 2.0f, errY},
          ERROR_LABEL_FONT_SIZE, FONT_SPACING, RAYLIB_RED
        );
        errY += errSize.y;
      }
    }
  }
};

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char *argv[]) {
  initApp("TSK Keyboard", 30);

  KeyboardUI keyboard;

  while (!WindowShouldClose()) {
    keyboard.update();

    BeginDrawing();
    ClearBackground(RAYLIB_BLACK);

    keyboard.draw();

    EndDrawing();

    if (keyboard.tappedHide()) {
      break;
    }
  }

  CloseWindow();
  return 0;
}
