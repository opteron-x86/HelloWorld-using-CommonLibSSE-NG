# Loot Drop System

SKSE plugin that implements a Diablo-style randomized loot system for Skyrim SE/AE/VR.

## Features

- Randomized loot drops from slain NPCs
- Configurable drop rates by item type
- Quest items and gold always drop
- Quality-based drop modifiers
- NPC type modifiers (bosses drop more loot)
- INI configuration file

## Installation

1. Install SKSE for your version of Skyrim
2. Copy `LootDropSystem.dll` to `Data/SKSE/Plugins/`
3. Configuration file auto-generates at `Documents/My Games/Skyrim Special Edition/SKSE/LootDropSystem.ini`

## Configuration

Edit `LootDropSystem.ini` to adjust drop rates:

```ini
[DropChances]
Armor=0.3       ; 30% chance armor pieces drop
Weapon=0.4      ; 40% chance weapons drop
Ammo=0.5        ; 50% chance arrows/bolts drop
Potion=0.6      ; 60% chance potions drop
Ingredient=0.7  ; 70% chance ingredients drop
Book=0.5        ; 50% chance books/scrolls drop
Misc=0.3        ; 30% chance misc items drop
SoulGem=0.4     ; 40% chance soul gems drop
Default=0.5     ; 50% for other items

[QualityMultipliers]
Enchanted=1.5   ; Enchanted items 1.5x more likely to drop
Unique=2.0      ; Unique items 2x more likely to drop
Daedric=1.8     ; Daedric items 1.8x more likely to drop

[NPCModifiers]
Boss=2.0        ; Boss NPCs drop 2x more loot
Elite=1.5       ; Elite NPCs drop 1.5x more loot
Bandit=0.8      ; Bandits drop 0.8x loot
```

## Building from Source

### Requirements

- Visual Studio 2022
- vcpkg (with VCPKG_ROOT environment variable set)
- CMake 3.21+

### Build Steps

1. Clone this repository
2. Open folder in Visual Studio 2022
3. CMake will configure automatically
4. Build → Build All

## Technical Details

The plugin hooks into `TESDeathEvent` to process NPC deaths. When an NPC dies:

1. Event handler validates the actor (skips player, essentials, summons)
2. Processes inventory after a short delay for death flags
3. Randomly determines which items to remove based on configured drop rates
4. Removes non-dropped items from the corpse

Quest items and gold are exempt from filtering to prevent quest breakage.

## Compatibility

- Works with Skyrim SE, AE, GOG, and VR (via CommonLibSSE-NG)
- Compatible with other loot mods that add items
- May conflict with mods that also remove items from corpses

## License

MIT License