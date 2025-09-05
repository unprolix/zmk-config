#!/usr/bin/env python3
"""
Shared functions for ZMK flashing tools
Extracted from auto-flash to avoid circular imports
"""

import os
import re
import subprocess
import sys
import time
import glob
import shutil
from pathlib import Path


def load_device_config():
    """Load device configuration from ~/.config/zmk/devices.yaml or devices.conf"""
    config_dir = Path.home() / ".config" / "zmk"
    yaml_path = config_dir / "devices.yaml"
    conf_path = config_dir / "devices.conf"
    devices = {}
    
    # Try YAML format first
    if yaml_path.exists():
        try:
            import yaml
            with open(yaml_path, 'r') as f:
                config = yaml.safe_load(f)
                if config:
                    for serial, device_info in config.items():
                        # Preserve all existing keys, not just name/type/notes
                        devices[serial] = dict(device_info)
                        # Ensure standard keys exist with defaults
                        devices[serial].setdefault('name', '')
                        devices[serial].setdefault('type', 'unknown')
                        devices[serial].setdefault('notes', '')
            return devices
        except ImportError:
            print("PyYAML not found, falling back to conf format")
        except Exception as e:
            print(f"Error reading YAML config: {e}, falling back to conf format")
    
    # Fall back to conf format
    if conf_path.exists():
        with open(conf_path, 'r') as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                
                parts = line.split(':', 3)
                if len(parts) < 2:
                    print(f"Warning: Invalid format at line {line_num}: {line}")
                    continue
                
                serial = parts[0]
                friendly_name = parts[1]
                device_type = parts[2] if len(parts) > 2 else "unknown"
                notes = parts[3] if len(parts) > 3 else ""
                
                devices[serial] = {
                    'name': friendly_name,
                    'type': device_type,
                    'notes': notes
                }
        return devices
    
    print("No config file found. Create ~/.config/zmk/devices.yaml or ~/.config/zmk/devices.conf")
    return devices


def get_usb_devices():
    """Get USB device information using lsusb -v"""
    try:
        result = subprocess.run(['lsusb', '-v'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        return result.stdout
    except FileNotFoundError:
        print("Error: lsusb command not found")
        return ""


def parse_usb_devices(lsusb_output):
    """Parse lsusb output to extract device information"""
    devices = []
    current_device = {}
    
    for line in lsusb_output.split('\n'):
        line = line.strip()
        
        if line.startswith('Bus ') and 'Device ' in line:
            if current_device:
                devices.append(current_device)
            current_device = {}
            # Extract bus and device numbers
            match = re.search(r'Bus (\d+) Device (\d+)', line)
            if match:
                current_device['bus'] = match.group(1).zfill(3)
                current_device['device'] = match.group(2).zfill(3)
        
        elif line.startswith('idVendor'):
            match = re.search(r'idVendor\s+0x([0-9a-f]+)\s*(.*)', line)
            if match:
                current_device['vendor_id'] = match.group(1)
                current_device['vendor_name'] = match.group(2).strip()
        
        elif line.startswith('idProduct'):
            match = re.search(r'idProduct\s+0x([0-9a-f]+)\s*(.*)', line)
            if match:
                current_device['product_id'] = match.group(1)
                current_device['product_name'] = match.group(2).strip()
        
        elif line.startswith('iSerial'):
            match = re.search(r'iSerial\s+\d+\s+(.+)', line)
            if match:
                current_device['serial'] = match.group(1).strip()
    
    if current_device:
        devices.append(current_device)
    
    return devices


def detect_bootloader_devices():
    """
    Detect devices currently in bootloader mode.
    Bootloader devices typically show up as USB mass storage devices.
    """
    bootloader_devices = []
    
    # Get USB devices
    lsusb_output = get_usb_devices()
    if not lsusb_output:
        return bootloader_devices
    
    usb_devices = parse_usb_devices(lsusb_output)
    
    # Look for devices that might be in bootloader mode
    bootloader_keywords = [
        'circuitpython', 'uf2', 'bootloader', 'rpi-rp2', 'pico',
        'adafruit', 'raspberry pi', 'seeed', 'nice!nano'
    ]
    
    for device in usb_devices:
        vendor_name = device.get('vendor_name', '').lower()
        product_name = device.get('product_name', '').lower()
        
        # Check if device matches bootloader patterns
        is_bootloader = any(keyword in vendor_name or keyword in product_name 
                          for keyword in bootloader_keywords)
        
        if is_bootloader:
            # Add device path information for mounting
            bus = device.get('bus')
            dev_num = device.get('device')
            if bus and dev_num:
                device['usb_path'] = f"/dev/bus/usb/{bus}/{dev_num}"
            bootloader_devices.append(device)
    
    return bootloader_devices


def find_available_mass_storage_devices():
    """
    Find unmounted mass storage devices that could be bootloaders.
    """
    try:
        # Use lsblk to find removable block devices
        result = subprocess.run(['lsblk', '-rno', 'NAME,TYPE,MOUNTPOINT,LABEL'], 
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        
        devices = []
        for line in result.stdout.strip().split('\n'):
            if not line:
                continue
            parts = line.split(None, 3)
            if len(parts) >= 3:
                name, device_type, mountpoint = parts[0], parts[1], parts[2]
                label = parts[3] if len(parts) > 3 else ""
                
                # Look for disk devices that are not mounted
                is_unmounted = (mountpoint == "" or 
                              (not mountpoint.startswith('/') and not mountpoint.startswith('[')))
                
                if (device_type == 'disk' and is_unmounted):
                    # Skip obvious system disks and swap
                    if (name.startswith('nvme') or 
                        name in ['sda', 'zram0'] or
                        'SWAP' in str(label) or
                        'luks-' in name):
                        continue
                    device_path = f"/dev/{name}"
                    devices.append({
                        'name': name,
                        'path': device_path,
                        'label': label,
                        'type': device_type
                    })
        
        return devices
        
    except FileNotFoundError:
        print("Warning: lsblk not found, cannot detect unmounted devices")
        return []


def find_bootloader_mount_points():
    """
    Find mounted bootloader devices by looking for UF2-compatible mount points.
    """
    mount_points = []
    
    # Common mount point locations
    search_paths = [
        "/media/*/",
        "/mnt/*/", 
        "/Volumes/*/",  # macOS
        "/run/media/*/*/",  # Some Linux distros
    ]
    
    bootloader_indicators = [
        "INFO_UF2.TXT",
        "INDEX.HTM",
        "CURRENT.UF2"
    ]
    
    for search_path in search_paths:
        for mount_point in glob.glob(search_path):
            if os.path.isdir(mount_point):
                # Check if this looks like a bootloader mount
                for indicator in bootloader_indicators:
                    if os.path.exists(os.path.join(mount_point, indicator)):
                        mount_points.append(mount_point.rstrip('/'))
                        break
    
    return mount_points


def get_bootloader_info(mount_point):
    """
    Extract information about the bootloader from INFO_UF2.TXT if available.
    """
    info_file = os.path.join(mount_point, "INFO_UF2.TXT")
    if not os.path.exists(info_file):
        return {}
    
    info = {}
    try:
        with open(info_file, 'r') as f:
            for line in f:
                if ':' in line:
                    key, value = line.strip().split(':', 1)
                    info[key.strip()] = value.strip()
    except Exception:
        pass
    
    return info


def flash_device(mount_point, firmware_file, device_name=None):
    """
    Flash firmware to a bootloader device.
    """
    if not os.path.exists(firmware_file):
        print(f"❌ Firmware file not found: {firmware_file}")
        return False
    
    target_file = os.path.join(mount_point, "CURRENT.UF2")
    
    try:
        print(f"📦 Flashing {os.path.basename(firmware_file)} to {mount_point}")
        
        # Copy firmware file
        shutil.copy2(firmware_file, target_file)
        
        # Sync to ensure write completes
        os.sync()
        
        print(f"✅ Successfully flashed {device_name or 'device'}")
        return True
        
    except Exception as e:
        print(f"❌ Failed to flash {device_name or 'device'}: {e}")
        return False


def mount_device(device_path, mount_point):
    """
    Mount a device to a mount point.
    """
    try:
        # Create mount point if it doesn't exist
        os.makedirs(mount_point, exist_ok=True)
        
        # Mount the device with proper permissions for flashing
        result = subprocess.run(['sudo', 'mount', '-o', 'rw,uid=1000,gid=1000', device_path, mount_point], 
                              capture_output=True, text=True)
        
        if result.returncode == 0:
            return True
        else:
            print(f"Failed to mount {device_path}: {result.stderr}")
            return False
            
    except Exception as e:
        print(f"Error mounting {device_path}: {e}")
        return False


def unmount_device(mount_point):
    """
    Unmount a device.
    """
    try:
        result = subprocess.run(['sudo', 'umount', mount_point], 
                              capture_output=True, text=True)
        return result.returncode == 0
    except Exception:
        return False


def scan_for_bootloaders(known_devices, firmware_dir, require_confirmation=False, scan_duration=5):
    """
    Continuously scan for bootloader devices for the specified duration.
    Returns list of (mount_point, device_config, firmware_file) tuples.
    """
    from auto_flash import find_flashable_devices_once
    
    start_time = time.time()
    scan_interval = 0.5  # Check every 500ms
    found_devices = set()  # Track already-found devices to avoid duplicates
    
    print(f"Scanning for bootloader devices for {scan_duration} seconds...")
    
    while time.time() - start_time < scan_duration:
        flashable = find_flashable_devices_once(known_devices, firmware_dir, require_confirmation, found_devices)
        
        if flashable:
            # Found devices, return immediately
            return flashable
        
        # Wait before next scan
        time.sleep(scan_interval)
    
    print("Scan complete. No devices found.")
    return []