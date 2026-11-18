import os
import sys
import torch
import torchaudio
import torch.nn.functional as F
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from light_siren_cnn import LightSirenCNN

MODEL_PATH = Path("checkpoints/light_siren/best.pt")
DATA_DIR = Path("real_world_horn_test_10s")

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

model = LightSirenCNN().to(device)
checkpoint = torch.load(MODEL_PATH, map_location=device)

if "model_state_dict" in checkpoint:
    model.load_state_dict(checkpoint["model_state_dict"])
elif "state_dict" in checkpoint:
    model.load_state_dict(checkpoint["state_dict"])
else:
    model.load_state_dict(checkpoint)

model.eval()

print("=" * 70)
print("LIGHT SIREN - REAL WORLD HORN TEST")
print("=" * 70)
print("Device:", device)
print("Files:", len(list(DATA_DIR.glob("*.wav"))))
print()

results = []

with torch.no_grad():
    for wav_path in sorted(DATA_DIR.glob("*.wav")):
        waveform, sr = torchaudio.load(str(wav_path))

        if waveform.shape[0] > 1:
            waveform = waveform.mean(dim=0, keepdim=True)

        if sr != 16000:
            waveform = torchaudio.functional.resample(waveform, sr, 16000)

        target_samples = 16000 * 10

        if waveform.shape[1] < target_samples:
            waveform = F.pad(waveform, (0, target_samples - waveform.shape[1]))
        else:
            waveform = waveform[:, :target_samples]

        mel = torchaudio.transforms.MelSpectrogram(
            sample_rate=16000,
            n_fft=1024,
            hop_length=256,
            n_mels=64
        )(waveform)

        mel = torch.log(mel + 1e-6)
        mel = F.interpolate(
            mel.unsqueeze(0),
            size=(128, 128),
            mode="bilinear",
            align_corners=False
        )

        output = model(mel.to(device))
        probability = torch.sigmoid(output).item()

        prediction = "SIREN" if probability >= 0.5 else "NON-SIREN"

        results.append((wav_path.name, probability, prediction))

        print(f"{wav_path.name}")
        print(f"  Probability: {probability:.4f} ({probability*100:.2f}%)")
        print(f"  Decision:    {prediction}")
        print()

false_alarms = sum(1 for _, _, p in results if p == "SIREN")

print("=" * 70)
print("REAL-WORLD HORN SUMMARY")
print("=" * 70)
print(f"Total files:       {len(results)}")
print(f"False alarms:      {false_alarms}")
print(f"Correct non-siren: {len(results) - false_alarms}")

if results:
    print(f"False alarm rate:  {false_alarms / len(results) * 100:.2f}%")
