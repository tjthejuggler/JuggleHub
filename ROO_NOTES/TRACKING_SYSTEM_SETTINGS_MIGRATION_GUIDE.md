# Tracking System Settings - Migration Guide

**Last Updated:** 2025-10-16  
**Target Audience:** Existing JuggleHub Users  
**Migration Type:** Automatic with Manual Verification

## Overview

This guide helps existing JuggleHub users understand and verify the migration from the old monolithic settings system to the new modular tracking system settings architecture.

### What Changed

The tracking system settings have been redesigned to support multiple tracker types with independent configurations:

**Before (Old System)**:
- Single `ball_settings.json` file for all settings
- All settings mixed together
- No tracker-specific configurations
- Manual save/load required

**After (New System)**:
- Separate settings file per tracker type
- `ball_settings.json` - 3D depth-based tracker
- `ball_settings_2d.json` - 2D simple tracker
- Automatic save/load per tracker
- Seamless tracker switching

---

## Table of Contents

1. [Who Needs to Migrate](#who-needs-to-migrate)
2. [What to Expect](#what-to-expect)
3. [Automatic Migration Process](#automatic-migration-process)
4. [Verification Steps](#verification-steps)
5. [Troubleshooting](#troubleshooting)
6. [Rollback Instructions](#rollback-instructions)
7. [FAQ](#faq)

---

## Who Needs to Migrate

### You Need to Migrate If:

✅ You have an existing JuggleHub installation  
✅ You have a `ball_settings.json` file in your project root  
✅ You've customized tracking settings  
✅ You're upgrading to the new version with modular settings

### You Don't Need to Migrate If:

❌ This is a fresh JuggleHub installation  
❌ You've never modified tracking settings  
❌ You're already using the new system (check for `ball_settings_2d.json`)

---

## What to Expect

### Before Migration

Your project structure looks like this:

```
JuggleHub/
├── ball_settings.json          # Your existing settings
└── hub/
    └── components/
        └── ui_settings.py      # Old monolithic settings UI
```

### After Migration

Your project structure will look like this:

```
JuggleHub/
├── ball_settings.json          # 3D tracker settings (migrated)
├── ball_settings_2d.json       # 2D tracker settings (new, defaults)
├── ball_settings_legacy.json   # Backup of your old settings
└── hub/
    └── components/
        ├── ui_settings.py              # Main settings coordinator
        ├── ui_settings_manager.py      # Settings management
        ├── ui_settings_common.py       # Common settings
        ├── ui_settings_3d.py           # 3D tracker settings
        └── ui_settings_2d.py           # 2D tracker settings
```

### What Gets Migrated

**Preserved Settings** (moved to `ball_settings.json`):
- All trajectory prediction settings
- Throw/catch detection parameters
- Color tracker weights
- Ball profile configurations
- YOLO detection thresholds
- Visualization preferences

**New Settings** (created with defaults):
- 2D tracker configuration in `ball_settings_2d.json`
- Tracker selection preference
- Per-tracker visualization options

**Backup Created**:
- Your original settings backed up to `ball_settings_legacy.json`

---

## Automatic Migration Process

### How It Works

The migration happens automatically when you first run the new version:

1. **Detection**: System detects old `ball_settings.json` format
2. **Backup**: Creates `ball_settings_legacy.json` backup
3. **Conversion**: Converts settings to new modular format
4. **Validation**: Verifies all settings preserved correctly
5. **Completion**: System ready to use with new architecture

### Migration Steps

**Step 1: Backup Your Settings (Recommended)**

Before upgrading, manually backup your settings:

```bash
# Create a backup directory
mkdir -p ~/jugglehub_backups

# Copy your current settings
cp ball_settings.json ~/jugglehub_backups/ball_settings_$(date +%Y%m%d).json

# Optional: Backup entire config
cp -r hub/config ~/jugglehub_backups/config_$(date +%Y%m%d)
```

**Step 2: Upgrade JuggleHub**

```bash
# Pull latest changes
git pull origin main

# Rebuild engine if needed
./scripts/build_engine.sh

# Update Python dependencies
./scripts/run_hub.sh --use-venv --install-deps
```

**Step 3: Launch and Verify**

```bash
# Start JuggleHub
./scripts/run_hub.sh --use-venv
```

The migration happens automatically on first launch. You'll see console output:

```
🔄 Detecting legacy settings format...
✅ Legacy settings detected
📦 Creating backup: ball_settings_legacy.json
🔄 Migrating settings to new format...
✅ Migration complete!
✅ 3D tracker settings: ball_settings.json
✅ 2D tracker settings: ball_settings_2d.json (defaults)
```

**Step 4: Verify Settings**

1. Open Calibration Mode
2. Check that all your settings are present
3. Test tracker switching (3D ↔ 2D)
4. Verify settings persist after restart

---

## Verification Steps

### 1. Check File Structure

Verify the new files exist:

```bash
# Check for new settings files
ls -la ball_settings*.json

# Expected output:
# ball_settings.json          # Your migrated 3D settings
# ball_settings_2d.json       # New 2D settings (defaults)
# ball_settings_legacy.json   # Backup of old settings
```

### 2. Verify Settings Content

**Check 3D Tracker Settings:**

```bash
# View migrated 3D settings
cat ball_settings.json | head -20
```

Expected structure:
```json
{
  "tracker_type": "3d",
  "trajectory": {
    "search_radius_base": 0.15,
    "search_radius_velocity_factor": 0.5
  },
  "throw_catch": {
    "throw_velocity_threshold": 0.5,
    "catch_distance_threshold": 0.15
  }
}
```

**Check 2D Tracker Settings:**

```bash
# View new 2D settings
cat ball_settings_2d.json
```

Expected structure:
```json
{
  "tracker_type": "2d",
  "detection": {
    "confidence_threshold": 0.45,
    "nms_threshold": 0.5
  }
}
```

### 3. Verify in UI

1. **Launch JuggleHub**:
   ```bash
   ./scripts/run_hub.sh --use-venv
   ```

2. **Open Calibration Mode**

3. **Check 3D Tracker Settings**:
   - Verify "Tracking System" dropdown shows "3D Depth-Based (SimpleBallTracker)"
   - Check trajectory settings match your old values
   - Verify throw/catch thresholds are correct
   - Confirm ball profiles are preserved

4. **Switch to 2D Tracker**:
   - Select "2D Simple (Simple2DBallTracker)" from dropdown
   - Verify UI updates to show 2D settings
   - Confirm 3D-specific sections are hidden

5. **Switch Back to 3D**:
   - Select "3D Depth-Based (SimpleBallTracker)"
   - Verify all your settings are still there
   - Confirm nothing was lost

### 4. Test Settings Persistence

1. **Modify a Setting**:
   - Change trajectory search radius
   - Note the new value

2. **Restart Application**:
   ```bash
   # Close JuggleHub
   # Restart
   ./scripts/run_hub.sh --use-venv
   ```

3. **Verify Setting Persisted**:
   - Open Calibration Mode
   - Check trajectory search radius
   - Should match your changed value

### 5. Test Tracker Switching

1. **Configure 3D Tracker**:
   - Set unique values for 3D settings
   - Note specific values

2. **Switch to 2D Tracker**:
   - Change 2D settings
   - Note specific values

3. **Switch Back to 3D**:
   - Verify 3D settings unchanged
   - Confirm values match step 1

4. **Switch to 2D Again**:
   - Verify 2D settings unchanged
   - Confirm values match step 2

---

## Troubleshooting

### Issue: Migration Didn't Run

**Symptoms**:
- No `ball_settings_legacy.json` created
- No console messages about migration
- Settings seem unchanged

**Solutions**:

1. **Check if already migrated**:
   ```bash
   # If this file exists, you're already migrated
   ls ball_settings_2d.json
   ```

2. **Force migration**:
   ```bash
   # Rename current settings to trigger migration
   mv ball_settings.json ball_settings_old.json
   mv ball_settings_old.json ball_settings.json
   
   # Restart JuggleHub
   ./scripts/run_hub.sh --use-venv
   ```

3. **Manual migration**:
   ```bash
   # Copy old settings to 3D tracker
   cp ball_settings.json ball_settings_3d_manual.json
   
   # Create default 2D settings
   echo '{"tracker_type":"2d","detection":{"confidence_threshold":0.45,"nms_threshold":0.5}}' > ball_settings_2d.json
   ```

### Issue: Settings Lost After Migration

**Symptoms**:
- Settings reset to defaults
- Custom values missing
- Ball profiles gone

**Solutions**:

1. **Restore from backup**:
   ```bash
   # Use the automatic backup
   cp ball_settings_legacy.json ball_settings.json
   
   # Or use your manual backup
   cp ~/jugglehub_backups/ball_settings_*.json ball_settings.json
   
   # Restart and try again
   ./scripts/run_hub.sh --use-venv
   ```

2. **Check backup file**:
   ```bash
   # Verify backup has your settings
   cat ball_settings_legacy.json | grep "search_radius"
   ```

3. **Manual settings transfer**:
   - Open `ball_settings_legacy.json`
   - Copy specific settings
   - Paste into `ball_settings.json`
   - Restart application

### Issue: Tracker Switching Not Working

**Symptoms**:
- Dropdown doesn't change tracker
- Settings don't update when switching
- UI doesn't respond

**Solutions**:

1. **Check console for errors**:
   ```bash
   # Run with debug output
   JUGGLEHUB_DEBUG=1 ./scripts/run_hub.sh --use-venv
   ```

2. **Verify settings files**:
   ```bash
   # Both files must exist
   ls -la ball_settings.json ball_settings_2d.json
   ```

3. **Reset to defaults**:
   ```bash
   # Backup current settings
   cp ball_settings.json ball_settings_backup.json
   cp ball_settings_2d.json ball_settings_2d_backup.json
   
   # Delete settings files
   rm ball_settings.json ball_settings_2d.json
   
   # Restart (will create defaults)
   ./scripts/run_hub.sh --use-venv
   
   # Manually restore your values from backups
   ```

### Issue: UI Shows Wrong Settings

**Symptoms**:
- Settings don't match file contents
- Values incorrect in UI
- Sliders at wrong positions

**Solutions**:

1. **Force settings reload**:
   - Close JuggleHub
   - Delete any `.pyc` files:
     ```bash
     find hub -name "*.pyc" -delete
     find hub -name "__pycache__" -type d -exec rm -rf {} +
     ```
   - Restart JuggleHub

2. **Verify JSON syntax**:
   ```bash
   # Check for JSON errors
   python3 -m json.tool ball_settings.json
   python3 -m json.tool ball_settings_2d.json
   ```

3. **Reset UI cache**:
   ```bash
   # Clear Qt settings cache
   rm -rf ~/.config/JuggleHub/
   
   # Restart application
   ./scripts/run_hub.sh --use-venv
   ```

---

## Rollback Instructions

If you need to revert to the old system:

### Option 1: Restore from Backup

```bash
# Stop JuggleHub

# Restore old settings
cp ball_settings_legacy.json ball_settings.json

# Remove new files
rm ball_settings_2d.json

# Checkout old code version
git checkout <previous-commit-hash>

# Rebuild
./scripts/build_engine.sh

# Restart
./scripts/run_hub.sh --use-venv
```

### Option 2: Use Manual Backup

```bash
# Stop JuggleHub

# Restore from your manual backup
cp ~/jugglehub_backups/ball_settings_*.json ball_settings.json

# Remove new files
rm ball_settings_2d.json ball_settings_legacy.json

# Checkout old code version
git checkout <previous-commit-hash>

# Rebuild and restart
./scripts/build_engine.sh
./scripts/run_hub.sh --use-venv
```

### Option 3: Fresh Start

```bash
# Stop JuggleHub

# Remove all settings
rm ball_settings*.json

# Checkout old code version
git checkout <previous-commit-hash>

# Rebuild
./scripts/build_engine.sh

# Restart (will create defaults)
./scripts/run_hub.sh --use-venv

# Manually reconfigure your settings
```

---

## FAQ

### Q: Will my ball color calibrations be preserved?

**A:** Yes, all ball profile settings including HSV ranges and enabled/disabled states are preserved in the migration.

### Q: Do I need to recalibrate anything?

**A:** No, all your calibrations are automatically migrated. You can start using the system immediately.

### Q: Can I use both trackers with the same settings?

**A:** No, each tracker has independent settings. This allows you to optimize settings per tracker type.

### Q: What happens to my old settings file?

**A:** It's backed up as `ball_settings_legacy.json` and remains untouched. You can always restore from it.

### Q: Can I manually edit the new settings files?

**A:** Yes, they're JSON files and can be edited manually. However, using the UI is recommended for safety.

### Q: Will future updates require migration again?

**A:** No, this is a one-time migration. Future updates will work with the new format.

### Q: Can I share settings between computers?

**A:** Yes, copy the `ball_settings.json` and `ball_settings_2d.json` files to other installations.

### Q: What if I have custom settings not in the UI?

**A:** Advanced settings in the JSON files are preserved even if not shown in UI. They'll continue to work.

### Q: Can I go back to the old system?

**A:** Yes, see the [Rollback Instructions](#rollback-instructions) section.

### Q: How do I know if migration was successful?

**A:** Check for:
1. `ball_settings_legacy.json` exists (backup)
2. `ball_settings_2d.json` exists (new 2D settings)
3. Console shows "Migration complete!" message
4. All your settings visible in UI

---

## Getting Help

### If You Encounter Issues

1. **Check Console Output**:
   ```bash
   JUGGLEHUB_DEBUG=1 ./scripts/run_hub.sh --use-venv 2>&1 | tee migration.log
   ```

2. **Verify File Integrity**:
   ```bash
   # Check JSON syntax
   python3 -m json.tool ball_settings.json
   python3 -m json.tool ball_settings_2d.json
   ```

3. **Review Documentation**:
   - User Guide: [`TRACKING_SYSTEM_SETTINGS_USER_GUIDE.md`](TRACKING_SYSTEM_SETTINGS_USER_GUIDE.md)
   - Architecture: [`TRACKING_SYSTEM_SETTINGS_ARCHITECTURE.md`](TRACKING_SYSTEM_SETTINGS_ARCHITECTURE.md)
   - Implementation: [`TRACKING_SYSTEM_SETTINGS_IMPLEMENTATION_SUMMARY.md`](TRACKING_SYSTEM_SETTINGS_IMPLEMENTATION_SUMMARY.md)

4. **Create GitHub Issue**:
   - Include migration.log
   - Include settings file contents (sanitized)
   - Describe what went wrong
   - Include steps to reproduce

### Support Resources

- **Documentation**: See related docs listed above
- **GitHub Issues**: Report problems or ask questions
- **Community**: Share experiences with other users

---

## Summary

### Migration Checklist

- [ ] Backup existing settings manually
- [ ] Upgrade to new version
- [ ] Launch JuggleHub (migration runs automatically)
- [ ] Verify `ball_settings_legacy.json` created
- [ ] Verify `ball_settings_2d.json` created
- [ ] Check all settings in UI
- [ ] Test tracker switching
- [ ] Test settings persistence
- [ ] Verify tracking works correctly
- [ ] Document any issues

### Key Points

✅ **Automatic**: Migration happens automatically on first launch  
✅ **Safe**: Original settings backed up to `ball_settings_legacy.json`  
✅ **Reversible**: Can rollback if needed  
✅ **Preserves Everything**: All settings, calibrations, and profiles migrated  
✅ **No Downtime**: System ready to use immediately after migration  

### Next Steps

After successful migration:

1. **Explore New Features**:
   - Try switching between trackers
   - Experiment with tracker-specific settings
   - Test settings persistence

2. **Optimize Settings**:
   - Fine-tune 3D tracker for your use case
   - Configure 2D tracker if needed
   - Save different configurations

3. **Provide Feedback**:
   - Report any issues
   - Suggest improvements
   - Share your experience

---

**Migration Guide Version:** 1.0  
**Last Updated:** 2025-10-16  
**Status:** Complete