import sys
import torch
import torchaudio
import torch.nn.functional as F
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from light_siren_cnn import LightSirenCNN

MODEL_PATH = Path("checkpoints/light_siren/best.pt")
DATA_DIR = Path("real_world_test_32k")

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
print("LIGHT SIREN - REAL WORLD SIREN TEST")
print("=" * 70)
print("Device:", device)
print()

total_positive = 0
correct_positive = 0
total_negative = 0
false_positive = 0

with torch.no_grad():
    for category in ["ambulance", "fire", "police", "music"]:
        folder = DATA_DIR / category

        for wav_path in sorted(folder.glob("*.wav")):
            waveform, sr = torchaudio.load(str(wav_path))

            if waveform.shape[0] > 1:
                waveform = waveform.mean(dim=0, keepdim=True)

            if sr != 16000:
                waveform = torchaudio.functional.resample(
                    waveform, sr, 16000
                )

            target_samples = 16000 * 10

            if waveform.shape[1] < target_samples:
                waveform = F.pad(
                    waveform, (0, target_samples - waveform.shape[1])
                )
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

            prediction = probability >= 0.5
            expected_positive = category != "music"

            if expected_positive:
                total_positive += 1
                if prediction:
                    correct_positive += 1
            else:
                total_negative += 1
                if prediction:
                    false_positive += 1

            print(f"{category.upper()}: {wav_path.name}")
            print(f"  Probability: {probability:.4f} ({probability*100:.2f}%)")
            print(f"  Decision:    {'SIREN' if prediction else 'NON-SIREN'}")
            print()

print("=" * 70)
print("REAL-WORLD SIREN SUMMARY")
print("=" * 70)
print(f"Positive siren files:     {total_positive}")
print(f"Detected correctly:       {correct_positive}")

if total_positive:
    print(f"Siren detection rate:     {correct_positive / total_positive * 100:.2f}%")

print(f"Negative music files:     {total_negative}")
print(f"Music false alarms:       {false_positive}")

if total_negative:
    print(f"Music false-alarm rate:   {false_positive / total_negative * 100:.2f}%")
