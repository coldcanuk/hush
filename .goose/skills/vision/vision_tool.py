#!/usr/bin/env python3
import sys
import os
import json
import base64
import urllib.request
import urllib.error

def main():
    if len(sys.argv) < 2:
        print("Usage: vision_tool.py <image_path> [prompt]")
        sys.exit(1)
        
    image_path = sys.argv[1]
    prompt = sys.argv[2] if len(sys.argv) > 2 else "Please describe this image in detail."
    
    if not os.path.exists(image_path):
        print(f"Error: Image not found at {image_path}")
        sys.exit(1)
        
    api_key = os.environ.get("CUSTOM_ACTVITE_API_KEY")
    if not api_key:
        print("Error: CUSTOM_ACTVITE_API_KEY environment variable is not set.")
        sys.exit(1)
        
    try:
        with open(image_path, "rb") as f:
            image_data = f.read()
            base64_image = base64.b64encode(image_data).decode("utf-8")
    except Exception as e:
        print(f"Error reading image: {e}")
        sys.exit(1)
        
    ext = os.path.splitext(image_path)[1].lower().strip('.')
    mime_type = f"image/{ext}" if ext in ["jpeg", "jpg", "png", "gif", "webp"] else "image/jpeg"
    
    url = "https://proxyllm-ext-node01.actvite.com/v1/chat/completions"
    
    payload = {
        "model": "qwen3-vl-8b-instruct",
        "messages": [
            {
                "role": "user",
                "content": [
                    {
                        "type": "text",
                        "text": prompt
                    },
                    {
                        "type": "image_url",
                        "image_url": {
                            "url": f"data:{mime_type};base64,{base64_image}"
                        }
                    }
                ]
            }
        ]
    }
    
    headers = {
        "Content-Type": "application/json",
        "Authorization": f"Bearer {api_key}",
        # Cloudflare (error 1010) blocks Python-urllib's default signature.
        "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36",
        "Accept": "application/json",
    }
    
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers=headers)
    
    try:
        with urllib.request.urlopen(req, timeout=60) as response:
            res_body = response.read().decode("utf-8")
            res_json = json.loads(res_body)
            print(res_json["choices"][0]["message"]["content"])
    except urllib.error.HTTPError as e:
        err_msg = e.read().decode('utf-8')
        print(f"HTTP Error {e.code}: {e.reason}\n{err_msg}")
        sys.exit(1)
    except Exception as e:
        print(f"Request failed: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
