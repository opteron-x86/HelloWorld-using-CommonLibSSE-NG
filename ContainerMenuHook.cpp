#include "ContainerMenuHook.h"
#include "LootManager.h"

// Alternative approach using menu event filter
class ContainerMenuFilter : public RE::MenuEventHandler {
public:
    static ContainerMenuFilter* GetSingleton() {
        static ContainerMenuFilter singleton;
        return &singleton;
    }
    
    bool ProcessButton(const RE::ButtonEvent* a_event) override {
        return false;
    }
    
    bool ProcessThumbstick(const RE::ThumbstickEvent* a_event) override {
        return false;
    }
    
    bool ProcessMouseMove(const RE::MouseMoveEvent* a_event) override {
        return false;
    }
    
    bool ProcessKinect(const RE::KinectEvent* a_event) override {
        return false;
    }
    
    bool ProcessDeviceConnect(const RE::DeviceConnectEvent* a_event) override {
        return false;
    }
    
    void FilterContainerItems(RE::ContainerMenu* a_menu) {
        if (!a_menu) return;
        
        auto* container = a_menu->GetContainerObject();
        if (!container) return;
        
        // Check if this is a dead actor
        auto* actor = container->As<RE::Actor>();
        if (!actor || !actor->IsDead()) return;
        
        // Get the inventory list from the menu
        auto& inventory = a_menu->itemList;
        auto* lootManager = LootManager::GetSingleton();
        
        // Filter items from display
        for (auto it = inventory.begin(); it != inventory.end();) {
            if (it->data && it->data->object) {
                auto* item = it->data->object->As<RE::TESBoundObject>();
                if (item && !lootManager->IsItemLootable(container->GetFormID(), item)) {
                    // Remove from display list
                    it = inventory.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }
};

// Hook using virtual function table override
class ContainerMenuHooks {
public:
    static void Install() {
        REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_ContainerMenu[0]};
        
        // Hook the menu open function
        _MenuOpenItem = vtbl.write_vfunc(0x1, MenuOpenItem);
        _ProcessMessage = vtbl.write_vfunc(0x4, ProcessMessage);
        
        SKSE::log::info("Container menu hooks installed");
    }
    
private:
    static void MenuOpenItem(RE::ContainerMenu* a_menu, std::int32_t a_arg1) {
        _MenuOpenItem(a_menu, a_arg1);
        
        // Filter items after menu opens
        FilterInventory(a_menu);
    }
    
    static RE::UI_MESSAGE_RESULTS ProcessMessage(RE::ContainerMenu* a_menu, RE::UIMessage& a_message) {
        // Refresh filter on inventory updates
        if (a_message.type == RE::UI_MESSAGE_TYPE::kInventoryUpdate) {
            FilterInventory(a_menu);
        }
        
        return _ProcessMessage(a_menu, a_message);
    }
    
    static void FilterInventory(RE::ContainerMenu* a_menu) {
        if (!a_menu) return;
        
        auto* container = a_menu->GetContainerObject();
        if (!container) return;
        
        // Only filter dead actors
        auto* actor = container->As<RE::Actor>();
        if (!actor || !actor->IsDead()) return;
        
        auto* lootManager = LootManager::GetSingleton();
        auto* itemList = a_menu->itemList.items;
        if (!itemList) return;
        
        // Create filtered list
        RE::SimpleArray<RE::ItemList::Item*> filteredItems;
        
        for (std::uint32_t i = 0; i < itemList->size(); ++i) {
            auto* item = (*itemList)[i];
            if (!item || !item->data.object) continue;
            
            auto* boundObject = item->data.object->As<RE::TESBoundObject>();
            if (boundObject && lootManager->IsItemLootable(container->GetFormID(), boundObject)) {
                filteredItems.push_back(item);
            }
        }
        
        // Replace item list with filtered version
        itemList->clear();
        for (auto* item : filteredItems) {
            itemList->push_back(item);
        }
        
        // Update UI
        auto* movie = a_menu->uiMovie;
        if (movie) {
            RE::GFxValue args[1];
            args[0].SetBoolean(true);
            movie->InvokeNoReturn("_root.Menu_mc.inventoryLists.itemList.InvalidateData", args, 1);
        }
    }
    
    static inline REL::Relocation<decltype(MenuOpenItem)> _MenuOpenItem;
    static inline REL::Relocation<decltype(ProcessMessage)> _ProcessMessage;
};

// Simple hook approach - intercept AddItem calls
class InventoryAddHook {
public:
    static void Install() {
        auto& trampoline = SKSE::GetTrampoline();
        
        // Hook TESObjectREFR::AddItem
        REL::Relocation<std::uintptr_t> hook{REL::ID(19230)};  // TESObjectREFR::AddItem
        _AddItem = trampoline.write_call<5>(hook.address() + 0x1A, AddItem);
        
        SKSE::log::info("Inventory add hook installed");
    }
    
private:
    static void AddItem(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_item, 
                       RE::ExtraDataList* a_extraList, std::int32_t a_count, 
                       RE::TESObjectREFR* a_fromRef) {
        
        // Check if player is looting from a dead actor
        if (a_ref && a_ref->IsPlayerRef() && a_fromRef) {
            auto* actor = a_fromRef->As<RE::Actor>();
            if (actor && actor->IsDead()) {
                auto* lootManager = LootManager::GetSingleton();
                
                // Block if item isn't lootable
                if (a_item && !lootManager->IsItemLootable(a_fromRef->GetFormID(), a_item)) {
                    // Show notification
                    RE::DebugNotification("This item cannot be looted");
                    return; // Don't add the item
                }
            }
        }
        
        // Call original function
        _AddItem(a_ref, a_item, a_extraList, a_count, a_fromRef);
    }
    
    static inline REL::Relocation<decltype(AddItem)> _AddItem;
};

void ContainerMenuHook::Install() {
    // Try multiple hook approaches for compatibility
    try {
        ContainerMenuHooks::Install();
    } catch (...) {
        SKSE::log::warn("Failed to install container menu hooks, trying inventory hook");
        try {
            InventoryAddHook::Install();
        } catch (...) {
            SKSE::log::error("Failed to install any hooks");
        }
    }
}