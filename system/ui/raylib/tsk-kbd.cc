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
// Helper: returns true if the user "tapped" (touch or left-click) inside 'rect'.
// -----------------------------------------------------------------------------
inline bool TappedInside(const Rectangle &rect) {
  return CheckCollisionPointRec(GetMousePosition(), rect)
         && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// -----------------------------------------------------------------------------
// Converts #RRGGBB to a Raylib Color (defaults to RAYLIB_GRAY if invalid).
// -----------------------------------------------------------------------------
Color HexToColor(const std::string &hex) {
  if (hex.size() != 7 || hex[0] != '#') {
    return RAYLIB_GRAY;
  }
  unsigned int r, g, b;
  if (sscanf(hex.c_str(), "#%02x%02x%02x", &r, &g, &b) != 3) {
    return RAYLIB_GRAY;
  }
  return {
    static_cast<unsigned char>(r),
    static_cast<unsigned char>(g),
    static_cast<unsigned char>(b),
    255
  };
}

// -----------------------------------------------------------------------------
// Writes content to a file. Returns (success, errorMessages).
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
// Reads the SecOCKey and returns "Installed: <key>", "Invalid", or "None".
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

  // Check for non-printable (binary) chars
  bool hasInvalidChars = std::any_of(
    buffer.begin(), buffer.end(),
    [](char c){ return (c < 32 && c != '\n' && c != '\r'); }
  );
  if (hasInvalidChars) {
    return "Installed: Invalid (binary file)";
  }

  // Remove newlines and validate 32-char hex
  std::string content(buffer.begin(), buffer.end());
  content.erase(std::remove(content.begin(), content.end(), '\n'), content.end());
  if (std::regex_match(content, std::regex("^[a-f0-9]{32}$"))) {
    return "Installed: " + content;
  }
  return "Installed: Invalid (" + content + ")";
}

// -----------------------------------------------------------------------------
// Reads and validates a 32-digit hex from a file. Returns empty if invalid.
// -----------------------------------------------------------------------------
std::string readAndValidateKeyFile(const std::string &filePath) {
  std::ifstream file(filePath);
  if (!file.is_open()) {
    return "";
  }
  std::string content;
  file >> content;
  return (std::regex_match(content, std::regex("^[a-f0-9]{32}$"))) ? content : "";
}

// -----------------------------------------------------------------------------
// Main entry
// -----------------------------------------------------------------------------
int main(int argc, char *argv[]) {
  // Initialize Raylib window
  initApp("TSK Keyboard", 30);

  // Load fonts
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

  // Determine initial inputText from two files; /data/params/d/SecOCKey has priority
  std::string persistKey = readAndValidateKeyFile("/persist/tsk/key");
  std::string secOCKey   = readAndValidateKeyFile("/data/params/d/SecOCKey");
  std::string inputText  = (!secOCKey.empty())
                             ? secOCKey
                             : ((!persistKey.empty()) ? persistKey : "");

  // "Hide" button geometry
  const std::string exitText = "Hide";
  Vector2 exitSize = MeasureTextEx(regularFont, exitText.c_str(), FONT_SIZE, FONT_SPACING);
  Rectangle exitRect = {
    static_cast<float>(GetScreenWidth() - exitSize.x - EXIT_BUTTON_PADDING),
    static_cast<float>(EXIT_BUTTON_PADDING / 2),
    exitSize.x + EXIT_BUTTON_PADDING,
    exitSize.y + EXIT_BUTTON_PADDING
  };

  // Keyboard geometry
  const int KEYBOARD_Y = GetScreenHeight() - KEY_HEIGHT * 2 - KEY_PADDING * 3;
  const int KEY_WIDTH_FIRST_ROW =
    (GetScreenWidth() - (NUM_KEYS_FIRST_ROW + 1) * KEY_PADDING) / NUM_KEYS_FIRST_ROW;
  const int KEY_WIDTH_SECOND_ROW =
    (GetScreenWidth() - (NUM_KEYS_SECOND_ROW + 1) * KEY_PADDING) / NUM_KEYS_SECOND_ROW;

  // Input box geometry
  float measureRef = MeasureTextEx(
    inputFont, "00000000000000000000000000000000",
    FONT_SIZE, INPUT_FONT_SPACING
  ).x;
  int inputBoxWidth  = static_cast<int>(measureRef);
  int inputBoxHeight = FONT_SIZE + INPUT_BOX_PADDING * 2;
  Rectangle inputBoxRect = {
    (GetScreenWidth() - inputBoxWidth) / 2.0f,
    (KEYBOARD_Y / 2.0f) - (inputBoxHeight / 2.0f),
    static_cast<float>(inputBoxWidth),
    static_cast<float>(inputBoxHeight)
  };

  // On-screen keyboard layout
  std::vector<std::string> keyTexts = {
    "1","2","3","4","5","6","7","8","9","0",
    "a","b","c","d","e","f","<"
  };
  std::vector<Rectangle> keyRects;
  keyRects.reserve(keyTexts.size());
  for (size_t i = 0; i < keyTexts.size(); ++i) {
    int row = (i < NUM_KEYS_FIRST_ROW) ? 0 : 1;
    int col = (row == 0) ? (int)i : (int)(i - NUM_KEYS_FIRST_ROW);
    int keyWidth = (row == 0) ? KEY_WIDTH_FIRST_ROW : KEY_WIDTH_SECOND_ROW;

    keyRects.push_back({
      static_cast<float>(KEY_PADDING + (keyWidth + KEY_PADDING) * col),
      static_cast<float>(KEYBOARD_Y + (KEY_HEIGHT + KEY_PADDING) * row),
      static_cast<float>(keyWidth),
      static_cast<float>(KEY_HEIGHT)
    });
  }

  // UI state
  bool showCharsLeftLabel = inputText.empty();
  bool showInstallButton  = false;
  bool showSuccessLabel   = false;

  Rectangle installRect;
  std::string installText   = "Install this key";
  std::string successText   = "Success!";
  std::string installedLabel = "Installed: None";
  std::vector<std::string> errorLines;

  // Update installed key label every second
  auto lastUpdateTime = std::chrono::steady_clock::now();

  // Main loop
  while (!WindowShouldClose()) {
    // Periodically refresh installed key status
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdateTime).count() >= 1) {
      installedLabel = readSecOCKey();
      lastUpdateTime = now;
    }

    // 1) Check if user tapped the "Hide" button
    if (TappedInside(exitRect)) {
      break;
    }

    // 2) Check if user tapped any keyboard key
    for (size_t i = 0; i < keyRects.size(); ++i) {
      if (TappedInside(keyRects[i])) {
        // If backspace
        if (keyTexts[i] == "<") {
          if (!inputText.empty()) {
            inputText.pop_back();
            showInstallButton  = false;
            showSuccessLabel   = false;
            showCharsLeftLabel = (inputText.size() < INPUT_BOX_CHARS);
            errorLines.clear();
          }
        }
        // Else if we have space left in input
        else if (inputText.size() < INPUT_BOX_CHARS) {
          inputText += keyTexts[i];
          showCharsLeftLabel = (inputText.size() < INPUT_BOX_CHARS);
        }
        // Stop scanning once we've registered this tap
        break;
      }
    }

    // 3) Decide if we can show the "Install" button
    showInstallButton  = (inputText.size() == INPUT_BOX_CHARS)
                         && (!showSuccessLabel && errorLines.empty());
    showCharsLeftLabel = (inputText.size() < INPUT_BOX_CHARS);

    // Re-calc install button rect if it should be shown
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

    // 4) If user taps the "Install" button, attempt to write the key
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
    // Drawing
    // -------------------------------------------------------------------------
    BeginDrawing();
    ClearBackground(RAYLIB_BLACK);

    // Installed key label
    Vector2 installedSize = MeasureTextEx(
      regularFont, installedLabel.c_str(), INSTALLED_LABEL_FONT_SIZE, FONT_SPACING
    );
    DrawTextEx(
      regularFont, installedLabel.c_str(),
      {(GetScreenWidth() - installedSize.x) / 2.0f,
       inputBoxRect.y - installedSize.y - CHARS_LEFT_LABEL_PADDING},
      INSTALLED_LABEL_FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
    );

    // Hide button
    DrawRectangleRec(exitRect, RAYLIB_GRAY);
    DrawTextEx(
      regularFont, exitText.c_str(),
      {exitRect.x + EXIT_BUTTON_PADDING / 2.0f,
       exitRect.y + EXIT_BUTTON_PADDING / 2.0f},
      FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
    );

    // Input box
    DrawRectangleRec(inputBoxRect, RAYLIB_BLACK);
    DrawRectangleLinesEx(inputBoxRect, 2, RAYLIB_RAYWHITE);

    // Display input text in 4-char color groups
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

    for (int i = 0; i < (int)inputText.size(); i += 4) {
      std::string group = inputText.substr(i, 4);
      Color groupColor  = colors[(i / 4) % (int)colors.size()];

      DrawTextEx(
        inputFont, group.c_str(),
        {textX, textY}, FONT_SIZE, INPUT_FONT_SPACING, groupColor
      );

      float groupW = MeasureTextEx(inputFont, group.c_str(),
                                   FONT_SIZE, INPUT_FONT_SPACING).x;
      textX += groupW + groupSpacing;
    }

    // Characters-left label (if not full)
    if (showCharsLeftLabel) {
      int charsLeft = INPUT_BOX_CHARS - (int)inputText.size();
      std::string charsLeftStr = std::to_string(charsLeft) + " characters left";
      Vector2 charsLeftSize    = MeasureTextEx(
        regularFont, charsLeftStr.c_str(), FONT_SIZE, FONT_SPACING
      );
      DrawTextEx(
        regularFont, charsLeftStr.c_str(),
        {
          (GetScreenWidth() - charsLeftSize.x) / 2.0f,
          inputBoxRect.y + inputBoxRect.height + CHARS_LEFT_LABEL_PADDING
        },
        FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
      );
    }

    // Install button (if needed)
    if (showInstallButton) {
      DrawRectangleRec(installRect, RAYLIB_GRAY);
      DrawTextEx(
        regularFont, installText.c_str(),
        {installRect.x + 20, installRect.y + 10},
        FONT_SIZE, FONT_SPACING, RAYLIB_RAYWHITE
      );
    }

    // Success label
    if (showSuccessLabel) {
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
      float errorY = showInstallButton
        ? installRect.y + installRect.height + CHARS_LEFT_LABEL_PADDING
        : inputBoxRect.y + inputBoxRect.height + CHARS_LEFT_LABEL_PADDING;

      for (const auto &line : errorLines) {
        Vector2 errSize = MeasureTextEx(
          regularFont, line.c_str(), ERROR_LABEL_FONT_SIZE, FONT_SPACING
        );
        DrawTextEx(
          regularFont, line.c_str(),
          {(GetScreenWidth() - errSize.x) / 2.0f, errorY},
          ERROR_LABEL_FONT_SIZE, FONT_SPACING, RAYLIB_RED
        );
        errorY += errSize.y;
      }
    }

    // Draw the on-screen keyboard keys
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

    EndDrawing();
  }

  // Cleanup
  UnloadFont(regularFont);
  UnloadFont(inputFont);
  CloseWindow();
  return 0;
}
