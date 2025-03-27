import re
import json

INPUT_FILE = "../../csr/content/ch12.md"
OUTPUT_FILE = "chunks.jsonl"

def load_and_chunk_markdown(path):
    with open(path, "r") as f:
        raw = f.read()

    # Split on ### headers or double newlines
    raw_chunks = re.split(r'(?:^|\n)(### .+?)\n|\n{2,}', raw)

    # Clean + dedupe
    chunks = [chunk.strip() for chunk in raw_chunks if chunk and chunk.strip()]
    
    structured = []
    for i, chunk in enumerate(chunks):
        structured.append({
            "id": f"chunk_{i}",
            "text": chunk,
            "metadata": {
                "source": "invocation_spec",
                "chunk_index": i
            }
        })

    return structured

def save_chunks_jsonl(chunks, path):
    with open(path, "w") as f:
        for item in chunks:
            f.write(json.dumps(item) + "\n")

if __name__ == "__main__":
    chunks = load_and_chunk_markdown(INPUT_FILE)
    save_chunks_jsonl(chunks, OUTPUT_FILE)
    print(f"✅ Chunked {len(chunks)} blocks from {INPUT_FILE} → {OUTPUT_FILE}")
