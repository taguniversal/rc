import os
import sqlite3
import uuid
from pinecone import Pinecone

# Load environment variables
PINECONE_API_KEY = os.getenv("PINECONE_API_KEY")
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY")  # in case you want embeddings later

# 🔧 Initialize Pinecone client (no more pinecone.init)
pc = Pinecone(api_key=PINECONE_API_KEY)
index = pc.Index("reality-compiler")  # Make sure this matches your actual index name

# 🗃️ Connect to SQLite DB
conn = sqlite3.connect("state/db.sqlite3")
cursor = conn.cursor()

# 📦 Fetch triples from the database
cursor.execute("SELECT subject, predicate, object FROM triples")
triples = cursor.fetchall()

# 💥 Dummy embed and upsert — using constant vectors for now
for subject, predicate, obj in triples:
    text = f"{subject} {predicate} {obj}"
    fake_vector = [0.0] * 1024  # Using LLaMA index, which expects 1024 dims

    index.upsert(vectors=[{
        "id": str(uuid.uuid4()),
        "values": fake_vector,
        "metadata": {"text": text}
    }])

print(f"✅ Uploaded {len(triples)} triples to Pinecone.")
