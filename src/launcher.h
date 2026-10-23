#pragma once
#include <string>
#include <toml++/toml.hpp>
#include "launcher_settings.h"

namespace shaolin::launcher {
bool Show();
bool ShouldPersist();
void Load(const void* table);
void Save(void* table);
bool Persist(const toml::table& table, std::string& error);
unsigned Language();
float AudioGain();
const Settings& Current();
}
