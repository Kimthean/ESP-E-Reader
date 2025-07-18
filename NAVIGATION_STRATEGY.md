# E-Reader Navigation Strategy

## Hardware Overview
The E-Reader has 3 physical buttons:
- **Button 1 (IO34)**: DOWN/NEXT button
- **Button 2 (IO39)**: SELECT/OK button  
- **Button 3 (IO35)**: UP/BACK button

## Navigation Philosophy
With only 3 buttons, we need a consistent and intuitive navigation system that works across all screens.

## Button Functions by Context

### Main Menu Screen
- **Button 3 (UP)**: Move selection up in menu
- **Button 1 (DOWN)**: Move selection down in menu
- **Button 2 (SELECT)**: Enter selected menu item

### Sub-Screens (Books, Settings, WiFi, Clock)
- **Button 3 (UP/BACK)**: Return to main menu
- **Button 1 (DOWN/NEXT)**: Navigate to next item/page (when applicable)
- **Button 2 (SELECT/OK)**: Confirm action or toggle setting

### List/Menu Screens (Future Implementation)
- **Button 3 (UP)**: Move up in list OR go back to parent menu
- **Button 1 (DOWN)**: Move down in list
- **Button 2 (SELECT)**: Select item or enter sub-menu

### Reading Screen (Future Implementation)
- **Button 3 (UP)**: Previous page
- **Button 1 (DOWN)**: Next page
- **Button 2 (SELECT)**: Open reading menu (bookmarks, settings, etc.)

## Advanced Button Actions

### Long Press Actions
- **Button 3 Long Press**: Always return to main menu (global back)
- **Button 2 Long Press**: Power menu (sleep, restart, shutdown)
- **Button 1 Long Press**: Quick settings or context menu

### Double Click Actions
- **Button 2 Double Click**: Quick action (e.g., toggle WiFi, bookmark page)
- **Button 3 Double Click**: Jump to last screen
- **Button 1 Double Click**: Jump to next major section

## Visual Feedback
Each screen should display button hints at the bottom:
```
[UP: Back] [SELECT: OK] [DOWN: Next]
```

## Implementation Guidelines

1. **Consistency**: Same button should perform similar actions across screens
2. **Predictability**: Users should know what each button does without looking
3. **Escape Route**: Button 3 (UP) should always provide a way to go back
4. **Context Sensitivity**: Button functions adapt to screen content
5. **Visual Cues**: Always show what buttons do on each screen

## Error Prevention
- Confirm destructive actions with button prompts
- Provide clear feedback for button presses
- Use different update modes for different types of navigation

## Implementation Status

- [x] Basic 3-button navigation
- [x] Context-aware button hints
- [x] Long press actions (SELECT long press = global back)
- [x] Double click shortcuts (placeholders ready)
- [x] Visual feedback system (button hints)
- [ ] Screen transition animations
- [x] Context-specific button handling
- [x] Placeholder screen implementations

## Future Enhancements
- Gesture support (if accelerometer is added)
- Button combination shortcuts
- Customizable button mappings in settings
- Haptic feedback (if vibration motor is added)