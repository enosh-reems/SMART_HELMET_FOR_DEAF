import os
import random
import torch
import torchaudio
import torch.nn.functional as F
import numpy as np

SEED = 42
random.seed(SEED)

ROOT = r"C:\Users\enosh\Downloads\e2panns\e2panns-main"

POS_ROOT = os.path.join(ROOT, "datasets", "AudioSet_EV_v2PANNs_2020", "Positive_files")
NEG_ROOT = os.path.join(ROOT, "datasets", "AudioSet_EV_v2PANNs_2020", "Negative_files")
REAL_ROOT = os.path.join(ROOT, "real_world_siren_train")
REAL_NEG_ROOT = os.path.join(ROOT, "real_world_hard_negatives")

BAD_FILES = {
    "eqnveHA0hrI.wav",
    "mW3S0u8bj58.wav",
}

def find_wavs(root):
    files = []
    for dirpath, _, filenames in os.walk(root):
        for f in filenames:
            if f.lower().endswith(".wav") and f not in BAD_FILES:
                files.append(os.path.join(dirpath, f))
    return files

def preprocess(path):
    waveform, sr = torchaudio.load(path)

    if waveform.shape[0] > 1:
        waveform = waveform.mean(dim=0, keepdim=True)

    if sr != 16000:
        waveform = torchaudio.functional.resample(
            waveform, sr, 16000
        )

    target_samples = 16000 * 10

    if waveform.shape[1] < target_samples:
        waveform = F.pad(
            waveform,
            (0, target_samples - waveform.shape[1])
        )
    else:
        waveform = waveform[:, :target_samples]

    mel_transform = torchaudio.transforms.MelSpectrogram(
        sample_rate=16000,
        n_fft=1024,
        hop_length=256,
        n_mels=64
    )

    mel = mel_transform(waveform)
    mel = torch.log(mel + 1e-6)

    mel = F.interpolate(
        mel.unsqueeze(0),
        size=(128, 128),
        mode="bilinear",
        align_corners=False
    ).squeeze(0)

    return mel.float()

print("Collecting audio files...")

positive = find_wavs(POS_ROOT)
negative = find_wavs(NEG_ROOT)
real_world = find_wavs(REAL_ROOT)
real_world_neg = find_wavs(REAL_NEG_ROOT)

print("AudioSet positive:", len(positive))
print("AudioSet negative:", len(negative))
print("Real-world sirens:", len(real_world))
print("Real-world hard negatives:", len(real_world_neg))

random.shuffle(positive)
random.shuffle(negative)
random.shuffle(real_world)
random.shuffle(real_world_neg)

# Representative calibration set:
# 6 AudioSet positives
# 6 AudioSet negatives
# 4 real-world sirens
# 4 real-world classroom/traffic negatives
selected = positive[:6] + negative[:6] + real_world[:4] + real_world_neg[:4]

print("Selected calibration files:", len(selected))

samples = []

for i, path in enumerate(selected):
    try:
        x = preprocess(path)
        samples.append(x.numpy())
        print(f"[{i+1}/{len(selected)}] OK:", os.path.basename(path))
    except Exception as e:
        print(f"[{i+1}/{len(selected)}] SKIP:", os.path.basename(path))
        print("   ", e)

if not samples:
    raise RuntimeError("No calibration samples were successfully created.")

data = np.stack(samples, axis=0).astype(np.float32)

out = os.path.join(
    ROOT,
    "models",
    "light_siren",
    "calibration_20.npz"
)

np.savez(out, mel_spectrogram=data)

print()
print("CALIBRATION DATA CREATED")
print("Shape:", data.shape)
print("Dtype:", data.dtype)
print("File:", out)
