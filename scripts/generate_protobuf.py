#!/usr/bin/env python3
"""
Generate nanopb C headers from Meshtastic .proto files
"""

import os
import subprocess
import sys

def find_nanopb_generator():
    """Find nanopb_generator.py in the nanopb library"""
    # Try to find in PlatformIO libs
    lib_paths = [
        os.path.join(os.getcwd(), ".pio", "libdeps", "tlora_pager_sx1262", "nanopb", "generator", "nanopb_generator.py"),
        os.path.join(os.getcwd(), ".pio", "libdeps", "tlora_pager_sx1262", "nanopb", "generator", "nanopb_generator.py"),
    ]
    
    for path in lib_paths:
        if os.path.exists(path):
            return path
    
    # Try system nanopb
    try:
        result = subprocess.run(["which", "nanopb_generator.py"], capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip()
    except:
        pass
    
    return None

def generate_protobuf():
    """Generate C headers from .proto files"""
    proto_dir = os.path.join(os.getcwd(), "lib", "meshtastic_protobufs")
    output_dir = os.path.join(os.getcwd(), "src", "chat", "infra", "meshtastic", "generated")
    
    # Create output directory
    os.makedirs(output_dir, exist_ok=True)
    
    # Find nanopb generator
    generator = find_nanopb_generator()
    if not generator:
        print("ERROR: nanopb_generator.py not found!")
        print("Please install nanopb library first: pio lib install nanopb")
        return False
    
    # Generate for each .proto file
    proto_files = [
        "meshtastic/portnums.proto",
        "meshtastic/channel.proto",
        "meshtastic/mesh.proto",
    ]
    
    for proto_file in proto_files:
        proto_path = os.path.join(proto_dir, proto_file)
        if not os.path.exists(proto_path):
            print(f"WARNING: {proto_path} not found, skipping")
            continue
        
        print(f"Generating from {proto_file}...")
        cmd = [
            sys.executable,
            generator,
            "-I", proto_dir,
            "-D", output_dir,
            proto_path
        ]
        
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, cwd=proto_dir)
            if result.returncode != 0:
                print(f"ERROR generating {proto_file}:")
                print(result.stderr)
                return False
        except Exception as e:
            print(f"ERROR: {e}")
            return False
    
    print("Protobuf generation complete!")
    return True

if __name__ == "__main__":
    if not generate_protobuf():
        sys.exit(1)
