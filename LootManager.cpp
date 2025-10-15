#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* scriptEventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (scriptEventSource) {
        scriptEventSource->AddEventSink<RE::TESDeathEvent>(this);
    }
    
    auto* uiEventSource = RE::UI::GetSingleton();
    if (uiEventSource) {
        uiEventSource->AddEventSink<RE::MenuOpenCloseEvent>(this);
    }
}

RE::BSEventNotifyControl LootManager::ProcessEvent(
    const RE::TESDeathEvent* a_event,
    RE::BSTEventSource<RE::TESDeathEvent>*) {
    
    if (!enabled || !a_event || !a_event->actorDying) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* actor = a_event->actorDying->As<RE::Actor>();
    
    if (actor && ShouldProcessActor(actor)) {
        ProcessActorDeath(actor);
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl LootManager::ProcessEvent(
    const RE::MenuOpenCloseEvent* a_event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
    
    if (!a_event || !a_event->opening) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    // Check if ContainerMenu is being opened
    if (a_event->menuName == RE::ContainerMenu::MENU_NAME) {
        FilterContainerMenu();
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    // Generate loot table immediately on death
    GenerateLootTable(a_actor);
}

bool LootManager::ShouldProcessActor(RE::Actor* a_actor) {
    if (!a_actor || a_actor->IsPlayerRef()) {
        return false;
    }
    
    // Skip essential actors
    if (a_actor->IsEssential()) {
        return false;
    }
    
    // Skip summons and temporary actors
    auto* base = a_actor->GetActorBase();
    if (base && base->IsSummonable()) {
        return false;
    }
    
    return true;
}

void LootManager::GenerateLootTable(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    std::lock_guard<std::mutex> lock(lootTableMutex);
    
    // Clear any existing table for this actor
    auto actorFormID = a_actor->GetFormID();
    lootableTables[actorFormID].clear();
    
    auto inventory = a_actor->GetInventory();
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        // Always make gold and quest items lootable
        if (item->IsGold()) {
            lootableTables[actorFormID].insert(item->GetFormID());
            continue;
        }
        
        // Check for quest items (keys, etc.)
        if (item->GetFormType() == RE::FormType::Misc) {
            if (auto* miscItem = item->As<RE::TESObjectMISC>()) {
                if (miscItem && miscItem->GetFullName() && 
                    std::string_view(miscItem->GetFullName()).find("Key") != std::string_view::npos) {
                    lootableTables[actorFormID].insert(item->GetFormID());
                    continue;
                }
            }
        }
        
        // Check drop chance for this item
        if (ShouldDropItem(item, a_actor)) {
            lootableTables[actorFormID].insert(item->GetFormID());
        }
    }
}

void LootManager::FilterContainerMenu() {
    auto* ui = RE::UI::GetSingleton();
    auto* containerMenu = ui ? ui->GetMenu<RE::ContainerMenu>() : nullptr;
    
    if (!containerMenu) return;
    
    // Get the container reference
    auto containerRef = containerMenu->GetContainerRef();
    if (!containerRef) return;
    
    auto* actor = containerRef->As<RE::Actor>();
    if (!actor || !actor->IsDead()) return;
    
    // Check if we have a loot table for this actor
    std::lock_guard<std::mutex> lock(lootTableMutex);
    auto it = lootableTables.find(actor->GetFormID());
    if (it == lootableTables.end()) return;
    
    const auto& lootableItems = it->second;
    
    // Get container inventory data from menu
    auto* inventoryData = containerMenu->itemList;
    if (!inventoryData) return;
    
    // Filter items - hide non-lootable items
    std::vector<RE::ItemList::Item*> itemsToHide;
    
    for (auto& item : inventoryData->items) {
        if (!item || !item->data.objDesc) continue;
        
        auto* boundObject = item->data.objDesc->object;
        if (!boundObject) continue;
        
        // If item is not in lootable table, mark for hiding
        if (lootableItems.find(boundObject->GetFormID()) == lootableItems.end()) {
            // Check stacked items - only hide the entire stack if none should drop
            bool anyLootable = false;
            if (item->data.objDesc->countDelta > 1) {
                // For stacks, check each item individually
                std::random_device rd;
                std::mt19937 gen(rd());
                
                for (std::int32_t i = 0; i < item->data.objDesc->countDelta; ++i) {
                    if (ShouldDropItem(boundObject, actor)) {
                        anyLootable = true;
                        break;
                    }
                }
            }
            
            if (!anyLootable) {
                itemsToHide.push_back(&item);
            }
        }
    }
    
    // Remove hidden items from display list
    for (auto* item : itemsToHide) {
        // Set count to 0 to hide from menu without removing from inventory
        if (item && item->data.objDesc) {
            item->data.objDesc->countDelta = 0;
        }
    }
    
    // Refresh menu display
    containerMenu->InvalidateDataSource();
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return true;
    
    // Always drop gold
    if (a_item->IsGold()) {
        return true;
    }
    
    // Check drop chance
    float dropChance = GetDropChance(a_item, a_actor);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    return dist(gen) < dropChance;
}

float LootManager::GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    auto* settings = Settings::GetSingleton();
    
    // Get base drop chance by item type
    float baseChance = settings->defaultDropChance;
    
    switch (a_item->GetFormType()) {
    case RE::FormType::Armor:
        baseChance = settings->armorDropChance;
        break;
        
    case RE::FormType::Weapon:
        baseChance = settings->weaponDropChance;
        break;
        
    case RE::FormType::Ammo:
        baseChance = settings->ammoDropChance;
        break;
        
    case RE::FormType::AlchemyItem:
        baseChance = settings->potionDropChance;
        break;
        
    case RE::FormType::Ingredient:
        baseChance = settings->ingredientDropChance;
        break;
        
    case RE::FormType::Book:
    case RE::FormType::Scroll:
        baseChance = settings->bookDropChance;
        break;
        
    case RE::FormType::Misc:
        baseChance = settings->miscDropChance;
        break;
        
    case RE::FormType::SoulGem:
        baseChance = settings->soulgemDropChance;
        break;
    }
    
    // Apply quality modifiers
    float multiplier = 1.0f;
    
    // Check if enchanted
    if (auto* enchantable = a_item->As<RE::TESEnchantableForm>()) {
        if (enchantable->formEnchanting) {
            multiplier *= settings->enchantedMultiplier;
        }
    }
    
    // Check if unique/legendary (high value)
    if (a_item->GetGoldValue() > 1000) {
        multiplier *= settings->uniqueMultiplier;
    }
    
    // Apply NPC type modifiers
    if (a_actor) {
        auto* base = a_actor->GetActorBase();
        if (base) {
            // Boss check (high level)
            if (a_actor->GetLevel() > 30) {
                multiplier *= settings->bossMultiplier;
            }
            // Bandit check
            else if (base->GetRace() && base->GetRace()->formEditorID.contains("Bandit")) {
                multiplier *= settings->banditMultiplier;
            }
        }
    }
    
    return std::min(1.0f, baseChance * multiplier);
}