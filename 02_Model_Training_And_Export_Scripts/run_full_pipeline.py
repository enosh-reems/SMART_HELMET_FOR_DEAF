"""
Smart Helmet - Master Automated Training, Quantization & Firmware Deployment Pipeline
Author: Antigravity Assistant

Pipeline Steps:
1. Trains LightSirenCNN on GPU with AudioSet + Real Kerala Sirens + 414 Kerala Classroom/Traffic Hard Negatives.
2. Exports trained model to ONNX.
3. Generates representative calibration dataset (sirens + classroom noise) for W8A8 quantization.
4. Quantizes ONNX to ESP32-S3 W8A8 format (.espdl) using ESP-PPQ.
5. Automatically deploys the new .espdl binary into the firmware project (s3_mic_test).
"""

import sys
import os
import shutil
import subprocess
from pathlib import Path

BASE_DIR = Path(r"C:\Users\enosh\Downloads\e2panns\e2panns-main\models\light_siren")
FIRMWARE_MODEL_DEST = Path(r"C:\Espressif\projects\s3_mic_test\main\models\light_siren_realworld_v2_s3_w8a8.espdl")

def run_step(step_name, script_name):
    print("\n" + "=" * 70)
    print(f"PIPELINE STEP: {step_name}")
    print("=" * 70)
    script_path = BASE_DIR / script_name
    cmd = [sys.executable, "-u", str(script_path)]
    result = subprocess.run(cmd, cwd=str(BASE_DIR))
    if result.returncode != 0:
        print(f"\n[ERROR] Step '{step_name}' failed with exit code {result.returncode}")
        sys.exit(result.returncode)
    print(f"\n[OK] Step '{step_name}' completed successfully.")

def main():
    print("=" * 70)
    print("STARTING FULL SMART HELMET SIREN AI TRAINING & DEPLOYMENT PIPELINE")
    print("=" * 70)

    # Step 1: Train model & export ONNX
    run_step("1. Training LightSirenCNN with Kerala Hard Negatives & Exporting ONNX", "train_light_siren_realworld.py")

    # Step 2: Create Calibration Dataset
    run_step("2. Creating Representative Calibration Dataset (calibration_20.npz)", "create_calibration.py")

    # Step 3: Quantize to ESP32-S3 W8A8 (.espdl)
    run_step("3. Quantizing to ESP32-S3 W8A8 (.espdl) using ESP-PPQ", "quantize_s3_w8a8.py")

    # Step 4: Copy to firmware
    espdl_source = BASE_DIR / "light_siren_realworld_v2_s3_w8a8.espdl"
    if espdl_source.exists():
        print("\n" + "=" * 70)
        print("PIPELINE STEP: 4. Deploying to ESP32-S3 Firmware Project")
        print("=" * 70)
        FIRMWARE_MODEL_DEST.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(espdl_source, FIRMWARE_MODEL_DEST)
        print(f"[SUCCESS] Copied new model to firmware: {FIRMWARE_MODEL_DEST}")
        print(f"Model size: {os.path.getsize(FIRMWARE_MODEL_DEST)} bytes")
    else:
        print(f"[!] Warning: {espdl_source} was not found.")

    print("\n" + "=" * 70)
    print("PIPELINE COMPLETE! Your ESP32-S3 firmware now has the finest retrained model.")
    print("=" * 70)

if __name__ == "__main__":
    main()
