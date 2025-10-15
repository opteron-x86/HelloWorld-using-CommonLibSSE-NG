#include "Settings.h"
#include <fstream>
#include <filesystem>
#include <Windows.h>
#include <ShlObj.h>

std::string Settings::GetConfigPath() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, path))) {
        std::filesystem::path configPath = path;
        configPath /= "My Games/Skyrim Special Edition/SKSE/LootDropSystem.ini";
        
        // Create directories if they don't exist
        std::filesystem::create_directories(configPath.parent_path());
        
        return configPath.string();
    }
    return "Data/SKSE/Plugins/LootDropSystem.ini";
}

void Settings::Load() {
    std::string configPath = GetConfigPath();
    
    // Use Windows INI functions for better compatibility
    auto getFloat = [&](const char* section, const char* key, float defaultValue) -> float {
        char buffer[32];
        GetPrivateProfileStringA(section, key, std::to_string(defaultValue).c_str(), 
                                buffer, sizeof(buffer), configPath.c_str());
        return std::stof(buffer);
    };
    
    // Load drop chances
    armorDropChance = getFloat("DropChances", "Armor", armorDropChance);
    weaponDropChance = getFloat("DropChances", "Weapon", weaponDropChance);
    ammoDropChance = getFloat("DropChances", "Ammo", ammoDropChance);
    potionDropChance = getFloat("DropChances", "Potion", potionDropChance);
    ingredientDropChance = getFloat("DropChances", "Ingredient", ingredientDropChance);
    bookDropChance = getFloat("DropChances", "Book", bookDropChance);
    miscDropChance = getFloat("DropChances", "Misc", miscDropChance);
    soulgemDropChance = getFloat("DropChances", "SoulGem", soulgemDropChance);
    defaultDropChance = getFloat("DropChances", "Default", defaultDropChance);
    
    // Load quality multipliers
    enchantedMultiplier = getFloat("QualityMultipliers", "Enchanted", enchantedMultiplier);
    uniqueMultiplier = getFloat("QualityMultipliers", "Unique", uniqueMultiplier);
    daedricMultiplier = getFloat("QualityMultipliers", "Daedric", daedricMultiplier);
    
    // Load NPC modifiers
    bossMultiplier = getFloat("NPCModifiers", "Boss", bossMultiplier);
    eliteMultiplier = getFloat("NPCModifiers", "Elite", eliteMultiplier);
    banditMultiplier = getFloat("NPCModifiers", "Bandit", banditMultiplier);
    
    // Create default config if it doesn't exist
    if (!std::filesystem::exists(configPath)) {
        Save();
    }
}

void Settings::Save() {
    std::string configPath = GetConfigPath();
    
    auto writeFloat = [&](const char* section, const char* key, float value) {
        WritePrivateProfileStringA(section, key, std::to_string(value).c_str(), configPath.c_str());
    };
    
    // Write header comment
    std::ofstream file(configPath);
    file << "; Loot Drop System Configuration\n";
    file << "; Values range from 0.0 (never drops) to 1.0 (always drops)\n\n";
    file.close();
    
    // Write drop chances
    writeFloat("DropChances", "Armor", armorDropChance);
    writeFloat("DropChances", "Weapon", weaponDropChance);
    writeFloat("DropChances", "Ammo", ammoDropChance);
    writeFloat("DropChances", "Potion", potionDropChance);
    writeFloat("DropChances", "Ingredient", ingredientDropChance);
    writeFloat("DropChances", "Book", bookDropChance);
    writeFloat("DropChances", "Misc", miscDropChance);
    writeFloat("DropChances", "SoulGem", soulgemDropChance);
    writeFloat("DropChances", "Default", defaultDropChance);
    
    // Write quality multipliers
    writeFloat("QualityMultipliers", "Enchanted", enchantedMultiplier);
    writeFloat("QualityMultipliers", "Unique", uniqueMultiplier);
    writeFloat("QualityMultipliers", "Daedric", daedricMultiplier);
    
    // Write NPC modifiers
    writeFloat("NPCModifiers", "Boss", bossMultiplier);
    writeFloat("NPCModifiers", "Elite", eliteMultiplier);
    writeFloat("NPCModifiers", "Bandit", banditMultiplier);
}