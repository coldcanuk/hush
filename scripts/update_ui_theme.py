import re

with open('hush-c/demo/index.html', 'r') as f:
    content = f.read()

# Replace light theme vars
content = re.sub(
    r'html\[data-theme="light"\] \{[^}]+\}',
    r'html[data-theme="light"] {\n'
    r'      --bg: oklch(98.8% 0.003 106.5); --surface: #ffffff; --surface-2: oklch(96.6% 0.005 106.5); --line: oklch(93% 0.007 106.5);\n'
    r'      --fg: oklch(22.8% 0.013 107.4); --muted: oklch(39.4% 0.023 107.4); --faint: oklch(46.6% 0.025 107.3); --accent: oklch(46.6% 0.025 107.3);\n'
    r'      --accent-ink: oklch(98.8% 0.003 106.5); --accent-dim: oklch(88% 0.011 106.6); --warn: #b91c1c;\n'
    r'    }',
    content
)

# Replace dark theme vars
content = re.sub(
    r':root, html\[data-theme="dark"\] \{[^}]+\}',
    r':root, html[data-theme="dark"] {\n'
    r'      --bg: oklch(15.3% 0.006 107.1); --surface: oklch(22.8% 0.013 107.4); --surface-2: oklch(28.6% 0.016 107.4); --line: oklch(39.4% 0.023 107.4);\n'
    r'      --fg: oklch(98.8% 0.003 106.5); --muted: oklch(88% 0.011 106.6); --faint: oklch(73.7% 0.021 106.9); --accent: oklch(58% 0.031 107.3);\n'
    r'      --accent-ink: oklch(15.3% 0.006 107.1); --accent-dim: oklch(46.6% 0.025 107.3); --warn: #f87171;\n'
    r'      --sans: "Inter", system-ui, sans-serif;\n'
    r'      --mono: ui-monospace, "Cascadia Mono", monospace;\n'
    r'      --display: "Instrument Serif", serif;\n'
    r'    }',
    content
)

# Apply fonts
content = content.replace('font-weight: 600;', 'font-weight: 600; font-family: var(--display);')
content = content.replace('font-size: 1.2rem;', 'font-size: 1.25rem; font-family: var(--display);')

# Make rectangles have rounded edges (not pill-shaped, not sharp)
content = re.sub(r'border-radius:\s*999px;', 'border-radius: 12px;', content)
content = re.sub(r'border-radius:\s*8px;', 'border-radius: 12px;', content)
content = re.sub(r'border-radius:\s*6px;', 'border-radius: 12px;', content)
content = re.sub(r'border-radius:\s*10px;', 'border-radius: 12px;', content)
content = re.sub(r'border-radius:\s*16px;', 'border-radius: 16px;', content)
content = re.sub(r'border-radius:\s*50%;', 'border-radius: 50%;', content) # keep circles as circles

with open('hush-c/demo/index.html', 'w') as f:
    f.write(content)
