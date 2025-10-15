#pragma once

class ContainerMenuHook {
public:
    static void Install();
    
private:
    static void ProcessInventoryList(RE::ContainerMenu* a_menu);
    static bool ShouldShowItem(RE::InventoryEntryData* a_entry, RE::TESObjectREFR* a_container);
    
    // Hook target
    static inline REL::Relocation<decltype(ProcessInventoryList)> _ProcessInventoryList;
};