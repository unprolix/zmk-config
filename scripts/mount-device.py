#!/usr/bin/env python3

import os
import sys
import argparse
import subprocess
import tempfile
import shutil
from pathlib import Path

# Constants
MAX_DEVICE_SIZE_MB = 64  # Maximum size for ZMK bootloader devices
REQUIRED_FILES = ['CURRENT.UF2', 'INFO_UF2.TXT']
INFO_FILE_FIELDS = ['UF2 Bootloader', 'Model:', 'Board-ID:', 'Date:']
MOUNT_OPTIONS = ['rw', 'uid=1000', 'gid=1000']

def run_command(cmd, check=True):
    """Execute shell command and return output"""
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, cmd, result.stderr)
    return result.stdout.strip(), result.stderr.strip(), result.returncode

def get_block_devices():
    """Get list of block devices with their sizes"""
    devices = []
    stdout, _, _ = run_command("lsblk -b -n -o NAME,SIZE,TYPE | grep disk")
    
    for line in stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            name = f"/dev/{parts[0]}"
            size_bytes = int(parts[1])
            size_mb = size_bytes / (1024 * 1024)
            if size_mb <= MAX_DEVICE_SIZE_MB:
                devices.append((name, size_mb))
    
    return devices

def is_mounted(device):
    """Check if device is already mounted"""
    stdout, _, _ = run_command(f"mount | grep '^{device}'", check=False)
    return bool(stdout)

def mount_device(device, mount_point):
    """Mount device to specified mount point"""
    mount_opts = ','.join(MOUNT_OPTIONS)
    cmd = f"sudo mount -o {mount_opts} {device} {mount_point}"
    _, stderr, returncode = run_command(cmd, check=False)
    return returncode == 0, stderr

def unmount_device(mount_point):
    """Unmount device at mount point"""
    cmd = f"sudo umount {mount_point}"
    _, stderr, returncode = run_command(cmd, check=False)
    return returncode == 0, stderr

def check_zmk_criteria(mount_point, verbose=False):
    """Check if mounted device meets ZMK bootloader criteria"""
    mount_path = Path(mount_point)
    
    # Check for required files
    for filename in REQUIRED_FILES:
        file_path = mount_path / filename
        if not file_path.exists():
            if verbose:
                print(f"  Missing required file: {filename}")
            return False
    
    # Check INFO_UF2.TXT content
    info_path = mount_path / 'INFO_UF2.TXT'
    try:
        with open(info_path, 'r') as f:
            content = f.read()
            
        found_fields = sum(1 for field in INFO_FILE_FIELDS if field in content)
        if found_fields < len(INFO_FILE_FIELDS) - 1:  # Allow one missing field
            if verbose:
                print(f"  INFO_UF2.TXT missing expected fields (found {found_fields}/{len(INFO_FILE_FIELDS)})")
            return False
            
    except Exception as e:
        if verbose:
            print(f"  Error reading INFO_UF2.TXT: {e}")
        return False
    
    return True

def process_device(device, size_mb, mount_location, no_mount, verbose):
    """Process a single device"""
    if verbose:
        print(f"\nChecking device: {device} ({size_mb:.1f} MB)")
    
    # Skip if already mounted
    if is_mounted(device):
        if verbose:
            print(f"  Device already mounted")
        return None
    
    # Create temporary mount point
    temp_mount = tempfile.mkdtemp(prefix="zmk_", dir="/tmp")
    
    try:
        # Try to mount device
        success, error = mount_device(device, temp_mount)
        if not success:
            if verbose:
                print(f"  Failed to mount: {error}")
            os.rmdir(temp_mount)
            return None
        
        # Check if it meets ZMK criteria
        if check_zmk_criteria(temp_mount, verbose):
            if verbose:
                print(f"  ✓ ZMK bootloader device detected")
            
            if no_mount:
                unmount_device(temp_mount)
                os.rmdir(temp_mount)
                return device
            else:
                # Unmount from temp location
                unmount_device(temp_mount)
                os.rmdir(temp_mount)
                
                # Mount at the specified location
                final_mount = os.path.expanduser(mount_location)
                os.makedirs(final_mount, exist_ok=True)
                
                success, error = mount_device(device, final_mount)
                
                if success:
                    print(f"Mounted ZMK device {device} at {final_mount}")
                    return final_mount
                else:
                    if verbose:
                        print(f"  Failed to mount at {final_mount}: {error}")
                    return None
        else:
            # Not a ZMK device, unmount
            if verbose:
                print(f"  Not a ZMK bootloader device")
            unmount_device(temp_mount)
            os.rmdir(temp_mount)
            return None
            
    except Exception as e:
        if verbose:
            print(f"  Error processing device: {e}")
        try:
            unmount_device(temp_mount)
            os.rmdir(temp_mount)
        except:
            pass
        return None

def main():
    parser = argparse.ArgumentParser(description='Find and mount ZMK bootloader devices')
    parser.add_argument('mount_location', help='Directory where the ZMK device should be mounted')
    parser.add_argument('--no-mount', action='store_true', help='Find devices but do not leave them mounted')
    parser.add_argument('--verbose', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    # Get candidate devices
    devices = get_block_devices()
    
    if args.verbose:
        print(f"Found {len(devices)} small block device(s) to check")
    
    # Process each device
    found_devices = []
    for device, size_mb in devices:
        result = process_device(device, size_mb, args.mount_location, args.no_mount, args.verbose)
        if result:
            found_devices.append(result)
            break  # Only mount one device at the specified location
    
    # Summary
    if found_devices:
        print(f"\nFound {len(found_devices)} ZMK bootloader device(s)")
    else:
        print("\nNo ZMK bootloader devices found")
    
    return 0 if found_devices else 1

if __name__ == "__main__":
    sys.exit(main())
