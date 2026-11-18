import os
import numpy as np
import torch
from torch.utils.data import DataLoader, TensorDataset

from esp_ppq import QuantizationSettingFactory
from esp_ppq.api import espdl_quantize_onnx

ROOT = r"C:\Users\enosh\Downloads\e2panns\e2panns-main"

ONNX_PATH = os.path.join(
    ROOT, "models", "light_siren",
    "light_siren_realworld_v2.onnx"
)

CALIB_PATH = os.path.join(
    ROOT, "models", "light_siren",
    "calibration_20.npz"
)

ESPDL_PATH = os.path.join(
    ROOT, "models", "light_siren",
    "light_siren_realworld_v2_s3_w8a8.espdl"
)

print("Loading calibration data...")
data = np.load(CALIB_PATH)["mel_spectrogram"]
data = torch.from_numpy(data).float()

print("Calibration shape:", tuple(data.shape))
print("Calibration dtype:", data.dtype)

dataset = TensorDataset(data)
dataloader = DataLoader(
    dataset,
    batch_size=1,
    shuffle=False
)

def collate_fn(batch):
    return batch[0].to("cpu")

print()
print("Starting ESP32-S3 W8A8 quantization...")
print("ONNX:", ONNX_PATH)
print("Output:", ESPDL_PATH)

quant_setting = QuantizationSettingFactory.espdl_setting()

quant_ppq_graph = espdl_quantize_onnx(
    onnx_import_file=ONNX_PATH,
    espdl_export_file=ESPDL_PATH,
    calib_dataloader=dataloader,
    calib_steps=len(data),
    input_shape=[1, 1, 128, 128],
    target="esp32s3",
    quant_type="w8a8",
    collate_fn=collate_fn,
    setting=quant_setting,
    device="cpu",
    error_report=True,
    skip_export=False,
    export_test_values=True,
    verbose=1,
)

print()
print("QUANTIZATION COMPLETE")
print("ESP-DL model:", ESPDL_PATH)
print("Exists:", os.path.exists(ESPDL_PATH))

if os.path.exists(ESPDL_PATH):
    print("Size:", os.path.getsize(ESPDL_PATH), "bytes")
