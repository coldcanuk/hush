---
name: vision
description: Analyze images using the custom actVite provider's qwen3-vl-8b-instruct model. Use this skill when asked to read, describe, or analyze an image file.
---

# Vision Skill

Because the default `deepseek-v4-pro` model does not support images, you must use this tool whenever you need to analyze, read, or look at an image. Do not use the native image attachment mechanism, as it will fail.

## Usage

When the user asks you to analyze an image, use the `vision_tool.py` script provided in this folder.

```bash
python3 .goose/skills/vision/vision_tool.py /path/to/image.png "Optional prompt to ask the vision model"
```

The script will securely read the image, send it to the `custom_actvite` vision model (`qwen3-vl-8b-instruct`), and print the description for you to read.

### Guidelines
1. Pass absolute paths to the tool whenever possible.
2. If the user doesn't provide a specific prompt, the tool will default to asking for a detailed description.
3. If the tool fails because `CUSTOM_ACTVITE_API_KEY` is missing, inform the user to set it or check their configuration.
