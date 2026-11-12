"""方策と価値のネットワーク（python/train/model.py）。"""
import torch
import torch.nn as nn

# 入力をだいたい 0〜1 にそろえるための割る数（チャンネルごとの最大値）
OBS_SCALE = torch.tensor([1, 1, 1, 1, 10, 10, 30, 400, 400, 1, 10, 1], dtype=torch.float32).view(1, 12, 1, 1)
SCAL_SCALE = torch.tensor([4, 4, 6000, 1, 1, 100], dtype=torch.float32)


class Block(nn.Module):
    """残差ブロック: 3x3 の畳み込みを 2 回して、入力に足す。"""

    def __init__(self, ch):
        super().__init__()
        self.c1 = nn.Conv2d(ch, ch, 3, padding=1)
        self.c2 = nn.Conv2d(ch, ch, 3, padding=1)

    def forward(self, x):
        return torch.relu(x + self.c2(torch.relu(self.c1(x))))


class Net(nn.Module):
    def __init__(self, ch=32):
        super().__init__()
        self.stem = nn.Conv2d(12 + 6, ch, 3, padding=1)  # 盤 12 枚 + scal 6 個（盤全体に広げる）
        self.blocks = nn.Sequential(Block(ch), Block(ch))
        self.move_head = nn.Conv2d(ch, 24, 1)  # マスごとに 方向 4 x 距離 6
        self.stay_head = nn.Linear(ch, 1)  # 「動かない」
        self.value_head = nn.Sequential(nn.Linear(ch, 64), nn.ReLU(), nn.Linear(64, 1))

    def forward(self, obs, scal, mask):
        """obs int16 [N,12,7,7]、scal int16 [N,6]、mask bool [N,1177] -> logits [N,1177]、value [N]"""
        x = obs.float() / OBS_SCALE.to(obs.device)
        s = scal.float() / SCAL_SCALE.to(obs.device)
        x = torch.cat([x, s[:, :, None, None].expand(-1, -1, 7, 7)], dim=1)
        h = self.blocks(torch.relu(self.stem(x)))
        g = h.mean(dim=(2, 3))
        # [N,24,7,7] -> [N,7,7,24] -> [N,1176]: 行動の番号 ((r*7+c)*4+d)*6+(dist-1) の順になる
        move = self.move_head(h).permute(0, 2, 3, 1).reshape(-1, 1176)
        logits = torch.cat([move, self.stay_head(g)], dim=1).masked_fill(~mask, -1e9)
        return logits, self.value_head(g).squeeze(1)
