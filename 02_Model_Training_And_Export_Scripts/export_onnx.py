import sys
from pathlib import Path
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from light_siren_cnn import LightSirenCNN

ROOT = Path(__file__).resolve().parents[2]
BEST_PATH = ROOT / "checkpoints" / "light_siren_realworld_v2" / "best.pt"
ONNX_PATH = Path(__file__).resolve().parent / "light_siren_realworld_v2.onnx"

print("Loading trained checkpoint from:", BEST_PATH)
checkpoint = torch.load(BEST_PATH, map_location="cpu")

model = LightSirenCNN()
model.load_state_dict(checkpoint["model_state_dict"])
model.eval()

dummy_input = torch.randn(1, 1, 128, 128)

print("Exporting to ONNX:", ONNX_PATH)
try:
    torch.onnx.export(
        model,
        dummy_input,
        str(ONNX_PATH),
        input_names=["mel_spectrogram"],
        output_names=["logit"],
        opset_version=13,
        dynamo=False
    )
except TypeError:
    torch.onnx.export(
        model,
        dummy_input,
        str(ONNX_PATH),
        input_names=["mel_spectrogram"],
        output_names=["logit"],
        opset_version=13
    )

print("SUCCESS: ONNX model exported!")
print("File size:", ONNX_PATH.stat().st_size, "bytes")
