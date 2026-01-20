#include "bsp_entity.h"

#include <cctype>
#include <iostream>

namespace ioq3_map {

std::vector<Entity> ParseBSPEntities(std::string_view entity_lump) {
  std::vector<Entity> entities;
  Entity current_entity;

  size_t cursor = 0;
  size_t n = entity_lump.size();

  auto skip_whitespace = [&]() {
    while (cursor < n &&
           std::isspace(static_cast<unsigned char>(entity_lump[cursor]))) {
      cursor++;
    }
  };

  auto peek = [&]() -> char {
    if (cursor < n) return entity_lump[cursor];
    return 0;
  };

  // Reads a quoted string. Returns empty string if next token is not a quoted
  // string.
  auto read_quoted_string = [&]() -> std::string {
    skip_whitespace();
    if (cursor >= n || entity_lump[cursor] != '"') {
      return "";
    }
    cursor++;  // Skip opening quote

    std::string token;
    token.reserve(64);

    while (cursor < n) {
      char c = entity_lump[cursor];
      if (c == '"') {
        break;
      }

      if (c == '\\' && cursor + 1 < n) {
        char next = entity_lump[cursor + 1];
        if (next == '"') {
          token.push_back('"');
          cursor += 2;
        } else if (next == '\\') {
          token.push_back('\\');
          cursor += 2;
        } else if (next == 'n') {
          token.push_back('\n');
          cursor += 2;
        } else {
          // Unknown escape, just keep the backslash and the character
          token.push_back(c);
          cursor++;
        }
      } else {
        token.push_back(c);
        cursor++;
      }
    }

    if (cursor < n) cursor++;  // Skip closing quote
    return token;
  };

  while (cursor < n) {
    skip_whitespace();
    if (cursor >= n) break;

    char c = peek();

    // Skip comments
    if (c == '/' && cursor + 1 < n && entity_lump[cursor + 1] == '/') {
      while (cursor < n && entity_lump[cursor] != '\n') cursor++;
      continue;
    }

    if (c == '{') {
      cursor++;  // enter entity
      // current_entity.clear();

      while (cursor < n) {
        skip_whitespace();
        // Check for end of entity
        if (peek() == '}') {
          cursor++;
          // entities.push_back(current_entity);
          break;
        }

        // Check for comments inside entity
        if (peek() == '/' && cursor + 1 < n && entity_lump[cursor + 1] == '/') {
          while (cursor < n && entity_lump[cursor] != '\n') cursor++;
          continue;
        }

        // Read key
        std::string key = read_quoted_string();
        if (key.empty()) {
          // If we hit something non-quoted that isn't '}', it might be junk or
          // malformed. Just advance to avoid infinite loop
          if (cursor < n && peek() != '}') cursor++;
          continue;
        }

        // Read value
        std::string value = read_quoted_string();

        // current_entity[key] = value;
      }
    } else {
      // Consume unexpected character to avoid loop
      cursor++;
    }
  }

  return entities;
}

void PrintBSPEntities(const std::vector<Entity>& entities) {
  std::cout << "Parsed " << entities.size() << " entities." << std::endl;
  for (size_t i = 0; i < entities.size(); ++i) {
    std::cout << "Entity " << i << ":" << std::endl;
    // for (const auto& [key, value] : entities[i]) {
    //   std::cout << "  \"" << key << "\" : \"" << value << "\"" << std::endl;
    // }
    std::cout << std::endl;
  }
}

}  // namespace ioq3_map
