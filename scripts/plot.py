import pandas as pd
import plotly.express as px
import sys
import os

if len(sys.argv) != 3:
    print("❌ Usage: python3 plot.py <predicate> <subject>")
    sys.exit(1)

predicate = sys.argv[1]
subject = sys.argv[2]

# Sanitize subject for filenames
safe_subject = subject.replace("/", "_")
input_csv = f"output/{predicate}_{safe_subject}.csv"
output_html = f"hugo-site/static/plots/{safe_subject.lower()}-{predicate.lower()}.html"

# Check that the CSV exists
if not os.path.exists(input_csv):
    print(f"❌ CSV file not found: {input_csv}")
    sys.exit(1)

# Load CSV
df = pd.read_csv(input_csv)

# Generate Plotly chart
title = f"{safe_subject.strip('_')} {predicate} Time Series"
fig = px.line(df, x='timestamp', y='value', title=title)

# Write HTML output
fig.write_html(output_html, full_html=True, include_plotlyjs='cdn')

print(f"✅ Plot written to {output_html}")
