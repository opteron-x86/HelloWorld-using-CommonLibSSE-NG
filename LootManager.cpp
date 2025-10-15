#include "LootManager.h"
#include "Settings.h"
#include <random>

void LootManager::Register() {
    auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventSource) {
        eventSource->AddEventSink(this);
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
        ProcessActorDeath(actor, nullptr);
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

void LootManager::ProcessActorDeath(RE::Actor* a_actor, RE::Actor*) {
    auto actorHandle = a_actor->GetHandle();
    
    std::thread([this, actorHandle]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto* actor = actorHandle.get().get();
        
        if (actor) {
            std::lock_guard<std::mutex> lock(processingMutex);
            FilterInventory(actor);
        }
    }).detach();
}

bool LootManager::ShouldProcessActor(RE::Actor* a_actor) {
    if (!a_actor || a_actor->IsPlayerRef()) {
        return false;
    }
    
    if (a_actor->IsEssential()) {
        return false;
    }
    
    auto* base = a_actor->GetActorBase();
    if (base && base->IsSummonable()) {
        return false;
    }
    
    return true;
}

bool LootManager::IsBodyArmor(RE::TESBoundObject* a_item) {
    if (!a_item || !a_item->Is(RE::FormType::Armor)) {
        return false;
    }
    
    auto* armor = a_item->As<RE::TESObjectARMO>();
    if (!armor) {
        return false;
    }
    
    using BipedSlot = RE::BGSBipedObjectForm::BipedObjectSlot;
    auto slotMask = armor->GetSlotMask();
    
    return (std::to_underlying(slotMask) & std::to_underlying(BipedSlot::kBody)) != 0;
}

void LootManager::AddReplacementClothing(RE::Actor* a_actor) {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) return;
    
    // "Belted Tunic" FormID 0x000A6D7D
    auto* clothing = dataHandler->LookupForm<RE::TESObjectARMO>(0x000A6D7D, "Skyrim.esm");
    if (clothing) {
        a_actor->AddObjectToContainer(clothing, nullptr, 1, nullptr);
        
        // Equip the clothing
        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (equipManager) {
            equipManager->EquipObject(a_actor, clothing);
        }
    }
}

void LootManager::FilterInventory(RE::Actor* a_actor) {
    if (!a_actor) return;
    
    auto inventory = a_actor->GetInventory();
    
    std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> itemsToRemove;
    bool removedBodyArmor = false;
    
    for (const auto& [item, data] : inventory) {
        auto& [count, entry] = data;
        
        if (!item || count <= 0) continue;
        
        if (item->IsGold()) {
            continue;
        }
        
        std::int32_t removeCount = 0;
        
        for (std::int32_t i = 0; i < count; ++i) {
            if (!ShouldDropItem(item, a_actor)) {
                removeCount++;
            }
        }
        
        if (removeCount > 0) {
            if (IsBodyArmor(item)) {
                removedBodyArmor = true;
            }
            
            itemsToRemove.push_back({item, removeCount});
        }
    }
    
    for (const auto& [item, count] : itemsToRemove) {
        a_actor->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
    }
    
    if (removedBodyArmor) {
        AddReplacementClothing(a_actor);
    }
}

bool LootManager::ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    if (!a_item) return true;
    
    if (a_item->IsGold()) {
        return true;
    }
    
    if (a_item->GetFormFlags() & RE::TESForm::RecordFlags::kMustUpdate) {
        return true;
    }
    
    float dropChance = GetDropChance(a_item, a_actor);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    return dist(gen) < dropChance;
}

float LootManager::GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor) {
    auto* settings = Settings::GetSingleton();
    
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
    
    float multiplier = 1.0f;
    
    if (auto* enchantable = a_item->As<RE::TESEnchantableForm>()) {
        if (enchantable->formEnchanting) {
            multiplier *= settings->enchantedMultiplier;
        }
    }
    
    if (a_item->GetGoldValue() > 1000) {
        multiplier *= settings->uniqueMultiplier;
    }
    
    if (a_actor) {
        if (a_actor->GetLevel() > 30) {
            multiplier *= settings->bossMultiplier;
        }
    }
    
    return std::min(1.0f, baseChance * multiplier);
}