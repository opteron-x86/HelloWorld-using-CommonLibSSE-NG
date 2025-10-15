#pragma once

class Settings {
public:
    static Settings* GetSingleton() {
        static Settings singleton;
        return &singleton;
    }
    
    void Load();
    void Save();
    
    // Drop chances per item type (0.0 to 1.0)
    float armorDropChance = 0.3f;
    float weaponDropChance = 0.4f;
    float ammoDropChance = 0.5f;
    float potionDropChance = 0.6f;
    float ingredientDropChance = 0.7f;
    float bookDropChance = 0.5f;
    float miscDropChance = 0.3f;
    float soulgemDropChance = 0.4f;
    float defaultDropChance = 0.5f;
    
    // Quality multipliers
    float enchantedMultiplier = 1.5f;
    float uniqueMultiplier = 2.0f;
    float daedricMultiplier = 1.8f;
    
    // NPC type modifiers
    float bossMultiplier = 2.0f;
    float eliteMultiplier = 1.5f;
    float banditMultiplier = 0.8f;
    
private:
    Settings() = default;
    ~Settings() = default;
    
    Settings(const Settings&) = delete;
    Settings(Settings&&) = delete;
    Settings& operator=(const Settings&) = delete;
    Settings& operator=(Settings&&) = delete;
    
    std::string GetConfigPath();
};