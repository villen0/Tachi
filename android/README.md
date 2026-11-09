# Revsense Nav (Android)

A small Android app that reads the **Google Maps navigation notification** and sends each turn to the Revsense board over Bluetooth.
The board shows it on its Navigation page (arrow, distance, street). Nothing is sent to the internet; it only talks to the board.

## Build and install (once)
1. Install **Android Studio**, choose *Open* and pick this `android/` folder. Let it sync (it downloads Gradle 8.7 and the Android SDK 34 pieces).
2. Plug in your phone (USB debugging on) or use *Run > Run 'app'* with a wireless-debugging device, and press Run.
3. On the phone, open **Revsense Nav**:
   - tap **1. Allow Bluetooth**;
   - tap **2. Allow notification access** and switch **Revsense Nav** on (Android asks for confirmation; this is how the app can read the Maps notification).
4. Start a route in Google Maps. The board's **Navigation** page appears (swipe to it) and follows the route.

The log on the app's screen shows every Maps notification (title | text | sub text) and every message sent to the board, which is the first thing to check
if a turn looks wrong: Google changes the notification layout now and then, and `MapsParser.kt` is the one place to adjust.

## What goes over Bluetooth
The board advertises a service called **Revsense** (`52657673-656e-7365-0000-4e4156000001`). The app writes short text lines to its characteristic
(`...0002`, write without response):

| message | meaning |
|---|---|
| `N|R|350|Main St` | a turn: code, metres to it, street (the street may be empty) |
| `C` | the route ended: clear the card |

Codes: `S` straight, `L` / `R` left / right, `SL` / `SR` slight, `HL` / `HR` sharp, `UL` / `UR` U-turn, `RB` roundabout, `A` arrive.

## Test the board without the app
Install **nRF Connect** on any phone, connect to **Revsense**, open the unknown service `52657673-...0001`, and write the text (as *Text/UTF-8*) `N|R|350|Main St`
to the characteristic. The Navigation page shows a right turn in 350 m (or 1150 ft); write `C` to clear it. Demo mode (hold the screen 1.5 s) also shows a
made-up route on that page.

## Tests
`app/src/test/.../MapsParserTest.kt` (JUnit): *Run > Run 'MapsParserTest'* in Android Studio.
