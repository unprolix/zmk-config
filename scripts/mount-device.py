#!/usr/bin/env python3

import os
import sys
import argparse
import subprocess
import tempfile
import time
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

# Constants
MAX_DEVICE_SIZE_MB = 64  # Maximum size for ZMK bootloader devices
REQUIRED_FILES = ['CURRENT.UF2', 'INFO_UF2.TXT']
INFO_FILE_FIELDS = ['UF2 Bootloader', 'Model:', 'Board-ID:', 'Date:']
MOUNT_OPTIONS = ['rw', 'uid=1000', 'gid=1000']
DEFAULT_WAIT_SECONDS = 5
DEVICE_CHECK_INTERVAL = 0.5  # How often to check for new devices
MAX_PARALLEL_MOUNTS = 8  # Maximum concurrent mount attempts

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

def extract_keyboard_info(mount_point):
   """Extract keyboard information from INFO_UF2.TXT"""
   info_path = Path(mount_point) / 'INFO_UF2.TXT'
   keyboard_info = {}
   
   try:
       with open(info_path, 'r') as f:
           content = f.read()
           
       # Extract model information
       for line in content.splitlines():
           line = line.strip()
           if line.startswith('Model:'):
               keyboard_info['model'] = line.split(':', 1)[1].strip()
           elif line.startswith('Board-ID:'):
               keyboard_info['board_id'] = line.split(':', 1)[1].strip()
           elif 'UF2 Bootloader' in line:
               keyboard_info['bootloader'] = True
               
   except Exception:
       pass
   
   return keyboard_info

def check_zmk_criteria(mount_point, verbose=False):
   """Check if mounted device meets ZMK bootloader criteria and return keyboard info"""
   mount_path = Path(mount_point)
   
   # Check for required files
   for filename in REQUIRED_FILES:
       file_path = mount_path / filename
       if not file_path.exists():
           if verbose:
               print(f"Missing required file: {filename}")
           return False, {}
   
   # Check INFO_UF2.TXT content and extract keyboard info
   info_path = mount_path / 'INFO_UF2.TXT'
   try:
       with open(info_path, 'r') as f:
           content = f.read()
           
       found_fields = sum(1 for field in INFO_FILE_FIELDS if field in content)
       if found_fields < len(INFO_FILE_FIELDS) - 1:  # Allow one missing field
           if verbose:
               print(f"INFO_UF2.TXT missing expected fields (found {found_fields}/{len(INFO_FILE_FIELDS)})")
           return False, {}
           
       keyboard_info = extract_keyboard_info(mount_point)
       return True, keyboard_info
           
   except Exception as e:
       if verbose:
           print(f"Error reading INFO_UF2.TXT: {e}")
       return False, {}

def check_device_zmk(device, size_mb, verbose=False):
   """Check if a device is a ZMK bootloader device and return keyboard info"""
   if verbose:
       print(f"\nChecking device: {device} ({size_mb:.1f} MB)")
   
   # Skip if already mounted
   if is_mounted(device):
       if verbose:
           print(f"Device already mounted")
       return False, None, {}
   
   # Create temporary mount point
   temp_mount = tempfile.mkdtemp(prefix="zmk_", dir="/tmp")
   
   try:
       # Try to mount device
       success, error = mount_device(device, temp_mount)
       if not success:
           if verbose:
               print(f"Failed to mount: {error}")
           os.rmdir(temp_mount)
           return False, None, {}
       
       is_zmk, keyboard_info = check_zmk_criteria(temp_mount, verbose)
       
       # Always unmount temp mount
       unmount_device(temp_mount)
       os.rmdir(temp_mount)
       
       if is_zmk and verbose:
           model = keyboard_info.get('model', 'Unknown')
           board_id = keyboard_info.get('board_id', 'Unknown')
           print(f"ZMK bootloader device detected: {model} (Board: {board_id})")
       elif verbose:
           print(f"Not a ZMK bootloader device")
           
       return is_zmk, device, keyboard_info
       
   except Exception as e:
       if verbose:
           print(f"Error processing device: {e}")
       try:
           unmount_device(temp_mount)
           os.rmdir(temp_mount)
       except:
           pass
       return False, None, {}

def mount_zmk_device(device, mount_location, verbose=False):
   """Mount a confirmed ZMK device to final location"""
   final_mount = os.path.expanduser(mount_location)
   os.makedirs(final_mount, exist_ok=True)
   
   success, error = mount_device(device, final_mount)
   
   if success:
       print(f"Mounted ZMK device {device} at {final_mount}")
       return final_mount
   else:
       if verbose:
           print(f"Failed to mount {device} at {final_mount}: {error}")
       return None

def match_keyboard_name(keyboard_info, target_name):
   """Check if keyboard info matches target keyboard name"""
   if not target_name:
       return True  # No filter, match any
       
   target_lower = target_name.lower()
   
   # Check model name
   model = keyboard_info.get('model', '').lower()
   if target_lower in model:
       return True
       
   # Check board ID  
   board_id = keyboard_info.get('board_id', '').lower()
   if target_lower in board_id:
       return True
       
   # Check for common keyboard name patterns
   keyboard_patterns = {
       'sofle': ['sofle'],
       'corne': ['corne', 'crkbd'],
       'glove80': ['glove80', 'glove'],
       'planck': ['planck'],
       'zen': ['zen', 'corneish']
   }
   
   for pattern_name, patterns in keyboard_patterns.items():
       if target_lower in pattern_name or any(p in target_lower for p in patterns):
           return any(p in model or p in board_id for p in patterns)
           
   return False

def scan_and_mount(mount_location, no_mount, verbose, wait_seconds, keyboard_name=None):
   """Scan for devices and mount ZMK device matching keyboard name"""
   start_time = time.time()
   checked_devices = set()
   
   while True:
       # Get current devices
       current_devices = get_block_devices()
       new_devices = [(dev, size) for dev, size in current_devices if dev not in checked_devices]
       
       if new_devices:
           if verbose and len(new_devices) > 0:
               print(f"\nScanning {len(new_devices)} new device(s)...")
           
           # Check devices in parallel
           with ThreadPoolExecutor(max_workers=min(len(new_devices), MAX_PARALLEL_MOUNTS)) as executor:
               futures = {
                   executor.submit(check_device_zmk, device, size_mb, verbose): (device, size_mb)
                   for device, size_mb in new_devices
               }
               
               for future in as_completed(futures):
                   device, size_mb = futures[future]
                   checked_devices.add(device)
                   
                   try:
                       is_zmk, found_device, keyboard_info = future.result()
                       if is_zmk and match_keyboard_name(keyboard_info, keyboard_name):
                           # Cancel remaining futures
                           for f in futures:
                               if f != future and not f.done():
                                   f.cancel()
                           
                           if no_mount:
                               return found_device, keyboard_info
                           else:
                               mount_path = mount_zmk_device(found_device, mount_location, verbose)
                               return mount_path, keyboard_info
                   except Exception as e:
                       if verbose:
                           print(f"Error checking {device}: {e}")
       
       # Check if we should continue waiting
       elapsed = time.time() - start_time
       if elapsed >= wait_seconds:
           break
           
       # Wait before next check
       time.sleep(DEVICE_CHECK_INTERVAL)
   
   return None, {}

def main():
   parser = argparse.ArgumentParser(description='Find and mount ZMK bootloader devices')
   parser.add_argument('mount_location', help='Directory where the ZMK device should be mounted')
   parser.add_argument('--keyboard', '-k', help='Keyboard name to match (sofle, corne, glove80, planck, zen)')
   parser.add_argument('--no-mount', action='store_true', help='Find devices but do not leave them mounted')
   parser.add_argument('--verbose', action='store_true', help='Verbose output')
   parser.add_argument('--wait', type=float, default=DEFAULT_WAIT_SECONDS, 
                      help=f'Seconds to wait for device (default: {DEFAULT_WAIT_SECONDS})')
   parser.add_argument('--no-wait', action='store_true', help='Do not wait for devices')
   
   args = parser.parse_args()
   
   # Determine wait time
   wait_seconds = 0 if args.no_wait else args.wait
   
   if args.verbose:
       keyboard_filter = f" matching '{args.keyboard}'" if args.keyboard else ""
       if wait_seconds > 0:
           print(f"Waiting up to {wait_seconds} seconds for ZMK bootloader devices{keyboard_filter}...")
       else:
           print(f"Scanning for ZMK bootloader devices{keyboard_filter}...")
   
   # Scan and mount
   result, keyboard_info = scan_and_mount(args.mount_location, args.no_mount, args.verbose, wait_seconds, args.keyboard)
   
   # Summary
   if result:
       model = keyboard_info.get('model', 'Unknown')
       board_id = keyboard_info.get('board_id', 'Unknown')
       action = 'identified' if args.no_mount else 'mounted'
       print(f"\nFound and {action} ZMK device: {model} (Board: {board_id})")
       return 0
   else:
       keyboard_filter = f" matching '{args.keyboard}'" if args.keyboard else ""
       print(f"\nNo ZMK bootloader devices found{keyboard_filter}")
       return 1

if __name__ == "__main__":
   sys.exit(main())
