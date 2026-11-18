import sys
import torch
import torchaudio
import torch.nn.functional as F
from pathlib import Path

BASE_DIR = Path(r"C:\Users\enosh\Downloads\e2panns\e2panns-main")
sys.path.insert(0, str(BASE_DIR / "models" / "light_siren"))
from light_siren_cnn import LightSirenCNN

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
model = LightSirenCNN().to(device)
ckpt_path = BASE_DIR / "checkpoints" / "light_siren_realworld_v2" / "best.pt"
ckpt = torch.load(ckpt_path, map_location=device)
model.load_state_dict(ckpt["model_state_dict"])
model.eval()

mel_transform = torchaudio.transforms.MelSpectrogram(
    sample_rate=16000, n_fft=1024, hop_length=256, n_mels=64
)

def eval_file(path):
    wf, sr = torchaudio.load(str(path))
    if wf.shape[0] > 1:
        wf = wf.mean(dim=0, keepdim=True)
    if sr != 16000:
        wf = torchaudio.functional.resample(wf, sr, 16000)
    target = 16000 * 10
    if wf.shape[1] < target:
        wf = F.pad(wf, (0, target - wf.shape[1]))
    else:
        wf = wf[:, :target]
    mel = mel_transform(wf)
    mel = torch.log(mel + 1e-6)
    mel = F.interpolate(mel.unsqueeze(0), size=(128, 128), mode="bilinear", align_corners=False)
    with torch.no_grad():
        out = model(mel.to(device))
        prob = torch.sigmoid(out).item()
        logit = out.item()
    return prob, logit

print("=== EVALUATION OF REAL WORLD SAMPLES ===")
# Test classroom chatter
neg_files = list((BASE_DIR / "real_world_hard_negatives" / "windows").glob("*.wav"))[:10]
print(f"\n--- Classroom / Speech Hard Negatives ({len(neg_files)} files) ---")
for f in neg_files:
    p, l = eval_file(f)
    print(f"{f.name[:45]:<45} : Prob={p:.4f} Logit={l:.2f}")

# Test real sirens
siren_files = list((BASE_DIR / "real_world_siren_train" / "windows").glob("*.wav"))[:10]
print(f"\n--- Real Sirens ({len(siren_files)} files) ---")
for f in siren_files:
    p, l = eval_file(f)
    print(f"{f.name[:45]:<45} : Prob={p:.4f} Logit={l:.2f}")

# Test horns
horn_dir = BASE_DIR / "real_world_horn_test_10s"
if horn_dir.exists():
    horn_files = list(horn_dir.glob("*.wav"))[:10]
    print(f"\n--- Real Horns ({len(horn_files)} files) ---")
    for f in horn_files:
        p, l = eval_file(f)
        print(f"{f.name[:45]:<45} : Prob={p:.4f} Logit={l:.2f}")
