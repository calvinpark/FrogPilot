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
// Constants
// -----------------------------------------------------------------------------
static const int FONT_SIZE                 = 100;
static const int INSTALLED_LABEL_FONT_SIZE = 80;
static const int ERROR_LABEL_FONT_SIZE     = 80;
static const float FONT_SPACING            = 1.0f;
static const float INPUT_FONT_SPACING      = 3.0f;

static const int EXIT_BUTTON_PADDING       = 20;
static const int INPUT_BOX_PADDING         = 20;
static const int CHARS_LEFT_LABEL_PADDING  = 20;

static const int KEY_HEIGHT                = 180;
static const int KEY_PADDING               = 10;
static const int NUM_KEYS_FIRST_ROW        = 10;
static const int NUM_KEYS_SECOND_ROW       = 7;
static const int INPUT_BOX_CHARS           = 32;

// -----------------------------------------------------------------------------
// Helper: returns true if the user "tapped" inside the given rect (touch or click).
// -----------------------------------------------------------------------------
inline bool TappedInside(const Rectangle &rect) {
  return CheckCollisionPointRec(GetMousePosition(), rect)
         && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// -----------------------------------------------------------------------------
// Convert a #RRGGBB string into a Raylib Color (default to RAYLIB_GRAY if invalid).
// -----------------------------------------------------------------------------
Color HexToColor(const std::string &hex) {
  if (hex.size() != 7 || hex[0] != '#') return RAYLIB_GRAY;
  unsigned int r, g, b;
  if (sscanf(hex.c_str(), "#%02x%02x%02x", &r, &g, &b) != 3) return RAYLIB_GRAY;
  return {
    static_cast<unsigned char>(r),
    static_cast<unsigned char>(g),
    static_cast<unsigned char>(b),
    255
  };
}

// -----------------------------------------------------------------------------
// Write content to a file, returning (success, errorMessages).
// -----------------------------------------------------------------------------
std::pair<bool, std::vector<std::string>> writeToFileWithError(
  const std::string &path,
  const std::string &content
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

// -----------------------------------------------------------------------------
// Reads the SecOCKey, returns "Installed: <key>", "Installed: Invalid(...)", or "Installed: None".
// -----------------------------------------------------------------------------
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

  // Check for non-printable chars
  bool hasInvalidChars = std::any_of(
    buffer.begin(), buffer.end(),
    [](char c){ return (c < 32 && c != '\n' && c != '\r'); }
  );
  if (hasInvalidChars) {
    return "Installed: Invalid (binary file)";
  }

  // Remove newlines, validate 32-char hex
  std::string content(buffer.begin(), buffer.end());
  content.erase(std::remove(content.begin(), content.end(), '\n'), content.end());
  if (std::regex_match(content, std::regex("^[a-f0-9]{32}$"))) {
    return "Installed: " + content;
  }
  return "Installed: Invalid (" + content + ")";
}

// -----------------------------------------------------------------------------
// Reads a 32-digit hex from file, or empty if invalid.
// -----------------------------------------------------------------------------
std::string readAndValidateKeyFile(const std::string &filePath) {
  std::ifstream file(filePath);
  if (!file.is_open()) return "";
  std::string content;
  file >> content;
  return std::regex_match(content, std::regex("^[a-f0-9]{32}$")) ? content : "";
}

// -----------------------------------------------------------------------------
// Helper: draw the installed label above the input box
// -----------------------------------------------------------------------------
void drawInstalledLabel(const Font &font,
                        const std::string &labelText,
                        const Rectangle &inputBoxRect) {
  Vector2 size = MeasureTextEx(font, labelText.c_str(),
                               INSTALLED_LABEL_FONT_SIZE, FONT_SPACING);
  DrawTextEx(
    font, labelText.c_str(),
    {
      (GetScreenWidth() - size.x) / 2.0f,
      inputBoxRect.y - size.y - CHARS_LEFT_LABEL_PADDING
    },
    INSTALLED_LABEL_FONT_SIZE,
    FONT_SPACING,
    RAYLIB_RAYWHITE
  );
}

// -----------------------------------------------------------------------------
// Helper: draw the on-screen keyboard keys
// -----------------------------------------------------------------------------
void drawKeyboard(const Font &font,
                  const std::vector<Rectangle> &keyRects,
                  const std::vector<std::string> &keyTexts) {
  for (size_t i = 0; i < keyRects.size(); ++i) {
    DrawRectangleRec(keyRects[i], RAYLIB_GRAY);
    Vector2 keySize = MeasureTextEx(font, keyTexts[i].c_str(),
                                    FONT_SIZE, FONT_SPACING);
    DrawTextEx(font, keyTexts[i].c_str(),
               {
                 keyRects[i].x + (keyRects[i].width - keySize.x) / 2.0f,
                 keyRects[i].y + (keyRects[i].height - keySize.y) / 2.0f
               },
               FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE);
  }
}

// -----------------------------------------------------------------------------
// Helper: draw the input box and text in color-coded 4-char groups
// -----------------------------------------------------------------------------
void drawInputArea(const Font &inputFont,
                   const Rectangle &inputBoxRect,
                   const std::string &inputText,
                   bool showCharsLeftLabel,
                   int inputBoxChars) {
  // 1. Input box background & border
  DrawRectangleRec(inputBoxRect, RAYLIB_BLACK);
  DrawRectangleLinesEx(inputBoxRect, 2, RAYLIB_RAYWHITE);

  // 2. Color-coded text
  float textX        = inputBoxRect.x + INPUT_BOX_PADDING;
  float textY        = inputBoxRect.y + INPUT_BOX_PADDING;
  float groupSpacing = MeasureTextEx(inputFont, " ", 30, INPUT_FONT_SPACING).x;

  std::vector<std::string> DARK_COLORS = {
    "#6A0DAD", "#2F4F4F", "#556B2F",
    "#8B0000", "#1874CD", "#006400"
  };
  std::vector<Color> colors;
  colors.reserve(DARK_COLORS.size());
  for (auto &hex : DARK_COLORS) {
    colors.push_back(HexToColor(hex));
  }

  // Render text in 4-char groups
  for (int i = 0; i < static_cast<int>(inputText.size()); i += 4) {
    std::string group = inputText.substr(i, 4);
    Color groupColor  = colors[(i / 4) % static_cast<int>(colors.size())];

    DrawTextEx(inputFont, group.c_str(), {textX, textY},
               FONT_SIZE, INPUT_FONT_SPACING, groupColor);

    float groupW = MeasureTextEx(inputFont, group.c_str(),
                                 FONT_SIZE, INPUT_FONT_SPACING).x;
    textX += groupW + groupSpacing;
  }

  // 3. Characters-left label
  if (showCharsLeftLabel) {
    int charsLeft = inputBoxChars - static_cast<int>(inputText.size());
    std::string charsLeftStr = std::to_string(charsLeft) + " characters left";
    Vector2 charsLeftSize    = MeasureTextEx(
      inputFont, charsLeftStr.c_str(), FONT_SIZE, FONT_SPACING
    );
    DrawTextEx(
      inputFont, charsLeftStr.c_str(),
      {
        (GetScreenWidth() - charsLeftSize.x) / 2.0f,
        inputBoxRect.y + inputBoxRect.height + CHARS_LEFT_LABEL_PADDING
      },
      FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
    );
  }
}

// -----------------------------------------------------------------------------
// Helper: draw success and error messages
// -----------------------------------------------------------------------------
void drawStatusMessages(const Font &font,
                        bool showSuccessLabel,
                        const std::string &successText,
                        const Rectangle &installRect,
                        const std::vector<std::string> &errorLines,
                        bool showInstallButton,
                        const Rectangle &inputBoxRect) {
  // Success label
  if (showSuccessLabel) {
    Vector2 successSize = MeasureTextEx(font, successText.c_str(),
                                        FONT_SIZE, FONT_SPACING);
    DrawTextEx(font, successText.c_str(),
               {
                 installRect.x + (installRect.width - successSize.x) / 2.0f,
                 installRect.y + (installRect.height - successSize.y) / 2.0f
               },
               FONT_SIZE, FONT_SPACING, RAYLIB_GREEN);
  }

  // Error messages
  if (!errorLines.empty()) {
    float errorY = showInstallButton
      ? installRect.y + installRect.height + CHARS_LEFT_LABEL_PADDING
      : inputBoxRect.y + inputBoxRect.height + CHARS_LEFT_LABEL_PADDING;

    for (auto &line : errorLines) {
      Vector2 errSize = MeasureTextEx(font, line.c_str(),
                                      ERROR_LABEL_FONT_SIZE, FONT_SPACING);
      DrawTextEx(font, line.c_str(),
                 {(GetScreenWidth() - errSize.x) / 2.0f, errorY},
                 ERROR_LABEL_FONT_SIZE, FONT_SPACING, RAYLIB_RED);
      errorY += errSize.y;
    }
  }
}

// -----------------------------------------------------------------------------
// Helper: draw exit/hide button and install button
// -----------------------------------------------------------------------------
void drawControlButtons(const Font &font,
                        const std::string &exitText,
                        const Rectangle &exitRect,
                        bool showInstallButton,
                        const std::string &installText,
                        const Rectangle &installRect) {
  // Hide button
  DrawRectangleRec(exitRect, RAYLIB_GRAY);
  DrawTextEx(
    font, exitText.c_str(),
    {exitRect.x + EXIT_BUTTON_PADDING / 2.0f,
     exitRect.y + EXIT_BUTTON_PADDING / 2.0f},
    FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
  );

  // Install button
  if (showInstallButton) {
    DrawRectangleRec(installRect, RAYLIB_GRAY);
    DrawTextEx(
      font, installText.c_str(),
      {installRect.x + 20, installRect.y + 10},
      FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
    );
  }
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char *argv[]) {
  initApp("TSK Keyboard", 30);

  // 1) Load fonts
  Font regularFont = LoadFontEx("/data/openpilot/selfdrive/assets/fonts/Inter-Regular.ttf",
                                FONT_SIZE * 2, nullptr, 0);
  SetTextureFilter(regularFont.texture, TEXTURE_FILTER_ANISOTROPIC_4X);
  if (!regularFont.baseSize) {
    TraceLog(LOG_ERROR, "Failed to load Inter-Regular.ttf");
    return 1;
  }

  Font inputFont = LoadFontEx("/data/openpilot/selfdrive/assets/fonts/Inter-Bold.ttf",
                              FONT_SIZE * 2, nullptr, 0);
  SetTextureFilter(inputFont.texture, TEXTURE_FILTER_ANISOTROPIC_4X);
  if (!inputFont.baseSize) {
    TraceLog(LOG_ERROR, "Failed to load Inter-Bold.ttf");
    return 1;
  }

  // 2) Determine initial inputText from two files; /data/params/d/SecOCKey has priority
  std::string persistKey = readAndValidateKeyFile("/persist/tsk/key");
  std::string secOCKey   = readAndValidateKeyFile("/data/params/d/SecOCKey");
  std::string inputText  = (!secOCKey.empty()) ? secOCKey
                                               : (!persistKey.empty() ? persistKey : "");

  // 3) Hide button geometry
  const std::string exitText = "Hide";
  Vector2 exitSize = MeasureTextEx(regularFont, exitText.c_str(), FONT_SIZE, FONT_SPACING);
  Rectangle exitRect = {
    static_cast<float>(GetScreenWidth() - exitSize.x - EXIT_BUTTON_PADDING),
    static_cast<float>(EXIT_BUTTON_PADDING / 2),
    exitSize.x + EXIT_BUTTON_PADDING,
    exitSize.y + EXIT_BUTTON_PADDING
  };

  // 4) Keyboard geometry
  const int KEYBOARD_Y = GetScreenHeight() - KEY_HEIGHT * 2 - KEY_PADDING * 3;
  const int KEY_WIDTH_FIRST_ROW =
    (GetScreenWidth() - (NUM_KEYS_FIRST_ROW + 1) * KEY_PADDING) / NUM_KEYS_FIRST_ROW;
  const int KEY_WIDTH_SECOND_ROW =
    (GetScreenWidth() - (NUM_KEYS_SECOND_ROW + 1) * KEY_PADDING) / NUM_KEYS_SECOND_ROW;

  // 5) Input box geometry
  float measureRef = MeasureTextEx(inputFont, "00000000000000000000000000000000",
                                   FONT_SIZE, INPUT_FONT_SPACING).x;
  int inputBoxWidth  = static_cast<int>(measureRef);
  int inputBoxHeight = FONT_SIZE + INPUT_BOX_PADDING * 2;
  Rectangle inputBoxRect = {
    (GetScreenWidth() - inputBoxWidth) / 2.0f,
    (KEYBOARD_Y / 2.0f) - (inputBoxHeight / 2.0f),
    static_cast<float>(inputBoxWidth),
    static_cast<float>(inputBoxHeight)
  };

  // 6) On-screen keyboard layout
  std::vector<std::string> keyTexts = {
    "1","2","3","4","5","6","7","8","9","0",
    "a","b","c","d","e","f","<"
  };
  std::vector<Rectangle> keyRects;
  keyRects.reserve(keyTexts.size());
  for (size_t i = 0; i < keyTexts.size(); ++i) {
    int row = (i < NUM_KEYS_FIRST_ROW) ? 0 : 1;
    int col = (row == 0) ? static_cast<int>(i)
                         : static_cast<int>(i - NUM_KEYS_FIRST_ROW);
    int keyWidth = (row == 0) ? KEY_WIDTH_FIRST_ROW : KEY_WIDTH_SECOND_ROW;

    keyRects.push_back({
      static_cast<float>(KEY_PADDING + (keyWidth + KEY_PADDING) * col),
      static_cast<float>(KEYBOARD_Y + (KEY_HEIGHT + KEY_PADDING) * row),
      static_cast<float>(keyWidth),
      static_cast<float>(KEY_HEIGHT)
    });
  }

  // 7) UI state
  bool showCharsLeftLabel = inputText.empty();
  bool showInstallButton  = false;
  bool showSuccessLabel   = false;

  Rectangle installRect;
  std::string installText   = "Install this key";
  std::string successText   = "Success!";
  std::string installedLabel = "Installed: None";
  std::vector<std::string> errorLines;

  // 8) Update installed key label every second
  auto lastUpdateTime = std::chrono::steady_clock::now();

  // ---------------------------------------------------------------------------
  // Main loop
  // ---------------------------------------------------------------------------
  while (!WindowShouldClose()) {
    // A) Re-check installed key periodically
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdateTime).count() >= 1) {
      installedLabel = readSecOCKey();
      lastUpdateTime = now;
    }

    // B) If user taps "Hide", break
    if (TappedInside(exitRect)) {
      break;
    }

    // C) Check keyboard key taps
    for (size_t i = 0; i < keyRects.size(); ++i) {
      if (TappedInside(keyRects[i])) {
        if (keyTexts[i] == "<" && !inputText.empty()) {
          inputText.pop_back();  // Backspace
          showInstallButton  = false;
          showSuccessLabel   = false;
          showCharsLeftLabel = (inputText.size() < INPUT_BOX_CHARS);
          errorLines.clear();
        }
        else if (keyTexts[i] != "<" && inputText.size() < INPUT_BOX_CHARS) {
          inputText += keyTexts[i];  // Add character
          showCharsLeftLabel = (inputText.size() < INPUT_BOX_CHARS);
        }
        break;
      }
    }

    // D) Update button states
    showInstallButton  = (inputText.size() == INPUT_BOX_CHARS)
                         && (!showSuccessLabel && errorLines.empty());
    showCharsLeftLabel = (inputText.size() < INPUT_BOX_CHARS);

    // Recalculate install button rect if needed
    if (showInstallButton) {
      Vector2 installSize = MeasureTextEx(regularFont, installText.c_str(),
                                          FONT_SIZE, FONT_SPACING);
      installRect = {
        (GetScreenWidth() - installSize.x - 40) / 2.0f,
        inputBoxRect.y + inputBoxRect.height + CHARS_LEFT_LABEL_PADDING,
        installSize.x + 40,
        installSize.y + 20
      };
    }

    // E) If user taps install, attempt to write the key
    if (showInstallButton && TappedInside(installRect)) {
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

    // -------------------------------------------------------------------------
    // F) Drawing
    // -------------------------------------------------------------------------
    BeginDrawing();
    ClearBackground(RAYLIB_BLACK);

    // 1) Installed label (now extracted to helper)
    drawInstalledLabel(regularFont, installedLabel, inputBoxRect);

    // 2) Control buttons
    drawControlButtons(regularFont, exitText, exitRect,
                       showInstallButton, installText, installRect);

    // 3) Input box & text
    drawInputArea(inputFont, inputBoxRect, inputText,
                  showCharsLeftLabel, INPUT_BOX_CHARS);

    // 4) Status messages (success/error)
    drawStatusMessages(regularFont, showSuccessLabel, successText,
                       installRect, errorLines, showInstallButton,
                       inputBoxRect);

    // 5) Keyboard
    drawKeyboard(regularFont, keyRects, keyTexts);

    EndDrawing();
  }

  // Cleanup
  UnloadFont(regularFont);
  UnloadFont(inputFont);
  CloseWindow();
  return 0;
}
