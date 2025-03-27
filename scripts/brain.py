import chromadb
import json
import os
from chromadb import PersistentClient
from chromadb.utils.embedding_functions import OpenAIEmbeddingFunction

# Your API key
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY") or "sk-..."  # Replace or set env var

# Init client + override embedding function
embedding_function = OpenAIEmbeddingFunction(
    api_key=OPENAI_API_KEY,
    model_name="text-embedding-3-small"
)

client = PersistentClient(path="./chroma_db")
collection_name = "invocation_spec"

# Load or create collection with embedder
try:
    collection = client.get_collection(collection_name)
except:
    collection = client.create_collection(collection_name, embedding_function=embedding_function)

# Load chunks
with open("chunks.jsonl", "r") as f:
    items = [json.loads(line) for line in f]

# Optional: delete by ID first to avoid duplicates
ids = [item["id"] for item in items]
collection.delete(ids=ids)

# Add docs
collection.add(
    documents=[item["text"] for item in items],
    metadatas=[item["metadata"] for item in items],
    ids=ids
)

print(f"🧠 Successfully loaded {len(items)} chunks using OpenAI embeddings.")
