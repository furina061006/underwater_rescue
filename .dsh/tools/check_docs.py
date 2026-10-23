#!/usr/bin/env python3
"""检查 winner_fish 的 agent 知识库结构。

检查项：
- 根索引、backlog、.gitignore 和 .dsh 目录存在；
- AGENTS.md 行数不超过 100；
- 五个主题文档都被 AGENTS.md 路由；
- 所有 Markdown 相对链接都能解析到存在的文件。

@file check_docs.py
@brief 知识库结构与链接检查器。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOPICS = ("hardware-safety", "architecture", "vision", "control", "operations")
REQUIRED_FILES = (ROOT / "AGENTS.md", ROOT / "README.md", ROOT / "backlog.md", ROOT / ".gitignore")
REQUIRED_DIRS = (ROOT / ".dsh" / "agents", ROOT / ".dsh" / "logs", ROOT / ".dsh" / "tools")
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
FENCE_RE = re.compile(r"```.*?```", re.S)


def strip_fences(text):
    """@brief 去掉 fenced code block，避免把示例里的链接当成真实链接。

    @param text Markdown 原文。
    @return 去掉代码块后的文本。
    """
    return FENCE_RE.sub("", text)


def check_links(path):
    """@brief 检查一个 Markdown 文件内的相对链接。

    @param path Markdown 文件路径。
    @return 错误信息列表；空列表表示全部通过。
    """
    errors = []
    text = strip_fences(path.read_text(encoding="utf-8"))
    for raw in LINK_RE.findall(text):
        target = raw.strip().strip("<>")
        if target.startswith(("http://", "https://", "mailto:", "#")):
            continue
        file_part = target.split("#", 1)[0]
        if not file_part:
            continue
        resolved = (path.parent / file_part).resolve()
        if not resolved.exists():
            errors.append(f"{path.relative_to(ROOT)} -> {target}")
    return errors


def main():
    """@brief 执行全部检查并打印结果。

    @return 0 表示通过，1 表示存在错误。
    """
    errors = []
    for path in REQUIRED_FILES:
        if not path.is_file():
            errors.append(f"missing file: {path.relative_to(ROOT)}")
    for path in REQUIRED_DIRS:
        if not path.is_dir():
            errors.append(f"missing directory: {path.relative_to(ROOT)}")

    agents = ROOT / "AGENTS.md"
    if agents.is_file():
        agents_text = agents.read_text(encoding="utf-8")
        line_count = len(agents_text.splitlines())
        if line_count > 100:
            errors.append(f"AGENTS.md is {line_count} lines (>100)")
        for topic in TOPICS:
            topic_path = ROOT / ".dsh" / "agents" / f"{topic}.md"
            if not topic_path.is_file():
                errors.append(f"missing topic: .dsh/agents/{topic}.md")
            elif f"{topic}.md" not in agents_text:
                errors.append(f"topic not routed from AGENTS.md: {topic}.md")
            elif "AGENTS.md" not in topic_path.read_text(encoding="utf-8"):
                errors.append(f"topic has no backlink to AGENTS.md: {topic}.md")

    markdown_files = [p for p in ROOT.rglob("*.md") if "build" not in p.parts and "runtime" not in p.parts]
    for path in markdown_files:
        errors.extend(check_links(path))

    if errors:
        print("check_docs: FAILED")
        for error in errors:
            print(" -", error)
        return 1
    print(f"check_docs: OK ({len(markdown_files)} markdown files, {len(TOPICS)} topics)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
