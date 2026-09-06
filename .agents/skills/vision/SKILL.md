---
name: vision
description: Analyze images using the custom actVite provider's qwen3-vl-8b-instruct model. Use when the user requests actVite image analysis or native image tools are unavailable.
---

# Vision Skill

Codex can use its available native image tools. This script is an optional fallback for sessions without image support or explicit actVite requests.

## Usage

For actVite analysis, use the `vision_tool.py` script provided in this folder.

```bash
python3 .agents/skills/vision/vision_tool.py /path/to/image.png "Optional prompt to ask the vision model"
```

The script will securely read the image, send it to the `custom_actvite` vision model (`qwen3-vl-8b-instruct`), and print the description for you to read.

### Guidelines
1. Pass absolute paths to the tool whenever possible.
2. If the user doesn't provide a specific prompt, the tool will default to asking for a detailed description.
3. If the tool fails because `CUSTOM_ACTVITE_API_KEY` is missing, inform the user to set it or check their configuration.
