# Controllers

The port uses SDL3 Gamepad for Xbox, PlayStation, and other mapped controllers.
Pair a Bluetooth controller in the system settings before use.
The port detects connections and disconnections while it runs.
Generic controller support depends on the system driver and the SDL mapping.
The game continues to display its original Xbox button prompts.

## Player assignment

In single-player mode, the keyboard, mouse and first controller control player 1.

With one controller at Ko-op entry, the keyboard and mouse control player 1. The controller controls player 2.
With two controllers connected when you enter Ko-op, the controllers control players 1 and 2.
The keyboard and mouse remain inactive for that assignment.
Assignments remain stable during disconnection so the other player keeps the same character.

Connect a controller for local Ko-op with the keyboard and mouse.

## Default keyboard mapping

| Key | Xbox input |
| --- | --- |
| W, A, S, D | Left stick |
| Arrow keys | Direction pad |
| Enter | Start |
| Backspace | Back |
| Space | A |
| Escape or Ctrl | B |
| J | X |
| K | Y |
| C | Black |
| Q | White |
| Shift | Left trigger |
| E | Right trigger |
| F9 | FPS and frame time display |
| F10 | Release or capture the mouse pointer |

## Mouse

Mouse movement controls the original right-stick camera input.
Scripted scenes and fixed camera areas can restrict movement.
Use the Mouse tab in the launcher to set sensitivity, vertical inversion, and button assignments.

| Mouse input | Default Xbox input |
| --- | --- |
| Movement | Right stick |
| Left button | X |
| Right button | B |
| Middle button | Y |
| Side button 1 | Black |
| Side button 2 | White |

Click inside the game or use the keyboard to capture the pointer.
The capture click does not trigger a game action.
F10 releases or captures the pointer.
Release the pointer to use the title bar and window buttons.
The game releases the pointer when it loses focus, minimizes or closes.

The captured pointer stays inside the game client area on the selected monitor.
Mouse and keyboard follow the same automatic player assignment.
Use the keyboard or controller for the original menus.

## PlayStation mapping

| PlayStation input | Xbox input |
| --- | --- |
| Cross | A |
| Circle | B |
| Square | X |
| Triangle | Y |
| Options | Start |
| Share | Back |
| L1 / R1 | White / Black |
| L2 / R2 | Left / right trigger |

Button positions follow the Xbox layout. Combat actions follow the configuration selected inside the original game.
An external input translator can expose both a physical controller and a virtual controller.
This can create a duplicate controller assignment.

## Configuration

Use the launcher to change dead zone, vibration, and button mappings.
You can place a custom `gamecontrollerdb.txt` in the application data directory.
The file uses the SDL mapping format. The port does not download mappings during play.
