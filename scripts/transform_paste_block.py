import re
from pathlib import Path

src = Path(r"E:/uclient-bc/BestClient-old/src/game/client/components/chat.cpp")
lines = src.read_text(encoding="utf-8").splitlines()
block = lines[2152:3103]

text = "\n".join(block)

text = re.sub(r"\bvoid CChat::", "void CUClientChatPasteImage::", text)
text = re.sub(r"\bbool CChat::", "bool CUClientChatPasteImage::", text)
text = re.sub(r"\bfloat CChat::", "float CUClientChatPasteImage::", text)


def add_pchat(match):
    sig = match.group(0)
    open_idx = sig.index("(")
    close_idx = sig.rindex(")")
    inner = sig[open_idx + 1 : close_idx].strip()
    if inner == "":
        return sig[: open_idx + 1] + "CChat *pChat" + sig[close_idx:]
    if inner.startswith("CChat *pChat"):
        return sig
    return sig[: open_idx + 1] + "CChat *pChat, " + sig[open_idx + 1 :]


for prefix in ("void", "bool", "float"):
    text = re.sub(rf"{prefix} CUClientChatPasteImage::\w+\([^)]*\)", add_pchat, text)

text = text.replace(
    "float CUClientChatPasteImage::PendingUploadPreviewHeight(CChat *pChat, float Width, float FontSize) const",
    "float CUClientChatPasteImage::PreviewHeight(CChat *pChat, float Width, float FontSize) const",
)

replacements = [
    ("Graphics()", "pChat->Graphics()"),
    ("Input()", "pChat->Input()"),
    ("Ui()", "pChat->Ui()"),
    ("TextRender()", "pChat->TextRender()"),
    ("Http()", "pChat->Http()"),
    ("Echo(", "pChat->Echo("),
    ("DisableMode()", "pChat->DisableMode()"),
    ("AddHistoryEntry(", "pChat->AddHistoryEntry("),
    ("SendChatPayloadQueued(", "pChat->SendChatPayloadQueued("),
    ("GameClient()", "pChat->GameClient()"),
    ("ChatMousePos()", "pChat->ChatMousePos()"),
    ("m_Mode == MODE_TEAM", "pChat->m_Mode == CChat::MODE_TEAM"),
    ("m_Input.", "pChat->m_Input."),
    ("m_SavedInputPending", "pChat->m_SavedInputPending"),
    ("m_aSavedInputText", "pChat->m_aSavedInputText"),
    ("m_pHistoryEntry", "pChat->m_pHistoryEntry"),
    ("MAX_LINE_LENGTH", "256"),
    ("RenderPendingUploadPreview(", "RenderPreview(pChat, "),
    ("PendingUploadPreviewHeight(", "PreviewHeight(pChat, "),
    ("UpdatePendingUpload()", "UpdatePendingUpload(pChat)"),
    ("ClearPendingUploadImage()", "ClearPendingUploadImage(pChat)"),
    ("SetPendingUploadImage(", "SetPendingUploadImage(pChat, "),
    ("TryPasteClipboardImage()", "TryPasteClipboardImage(pChat)"),
    ("StartPendingUpload(", "StartPendingUpload(pChat, "),
    ("OpenImageEditor()", "OpenImageEditor(pChat)"),
    ("CancelImageEditor()", "CancelImageEditor(pChat)"),
    ("SaveImageEditorChanges()", "SaveImageEditorChanges(pChat)"),
    ("UpdateImageEditorInput()", "UpdateImageEditorInput(pChat)"),
    ("RenderImageEditor()", "RenderImageEditor(pChat)"),
    ("SRenderRect", "CUClientChatPasteImage::SRenderRect"),
    ("SImageEditorStroke", "CUClientChatPasteImage::SImageEditorStroke"),
    ("EImageEditorTool", "CUClientChatPasteImage::EImageEditorTool"),
    ("EPendingUploadState", "CUClientChatPasteImage::EPendingUploadState"),
]

for old, new in replacements:
    text = text.replace(old, new)

out = Path(r"E:/uclient-bc/BestClient/src/game/client/components/uclient/_paste_block.cpp")
out.write_text(text, encoding="utf-8")
print(f"Wrote {len(text.splitlines())} lines")
