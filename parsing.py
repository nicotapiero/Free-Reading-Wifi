import os
import re
import shutil
from bs4 import BeautifulSoup

# --- CONFIG ---
BASE_DIR = "./boooks"  # directory containing your pgXXXX-h folders
HTML_FILE = "./list.html"  # save your provided HTML into this file

# --- STEP 1: parse the HTML list ---
with open(HTML_FILE, "r", encoding="utf-8") as f:
    soup = BeautifulSoup(f, "html.parser")

books = []

for i, li in enumerate(soup.find_all("li"), start=1):
    a = li.find("a")
    href = a["href"]
    text = a.get_text()

    # extract ID from /ebooks/<id>
    match = re.search(r"/ebooks/(\d+)", href)
    if not match:
        continue
    book_id = match.group(1)

    # split title and author
    if " by " in text:
        title, author = text.split(" by ", 1)
    else:
        title, author = text, "Unknown"

    # remove trailing "(12345)" from author
    author = re.sub(r"\s*\(\d+\)\s*$", "", author).strip()

    # clean title just in case
    title = title.strip()

    books.append({
        "index": i,
        "id": book_id,
        "title": title.strip(),
        "author": author.strip()
    })

# --- STEP 2: process folders ---
for book in books:
    old_folder = os.path.join(BASE_DIR, f"pg{book['id']}-h")
    
    if not os.path.isdir(old_folder):
        print(f"Skipping missing: {old_folder}")
        continue

    index_str = f"{book['index']:02d}"

    # enforce max length for ID (6 digits)
    if len(book['id']) > 6:
        raise ValueError(
            f"ID too long for 8.3 format: {book['id']} (book #{book['index']}: {book['title']})"
        )

    id_str = f"{int(book['id']):06d}"

    new_folder_name = index_str + id_str
    new_folder = os.path.join(BASE_DIR, new_folder_name)

    print(f"Processing {old_folder} -> {new_folder}")

    # rename folder
    os.rename(old_folder, new_folder)

    # find the main HTML file
    html_file = None
    for file in os.listdir(new_folder):
        if file.lower().endswith(".htm") or file.lower().endswith(".html"):
            html_file = file
            break

    if html_file:
        old_html_path = os.path.join(new_folder, html_file)
        new_html_path = os.path.join(new_folder, "book.htm")
        os.rename(old_html_path, new_html_path)
    else:
        print(f"WARNING: No HTML file found in {new_folder}")

    # remove everything except book.htm
    for item in os.listdir(new_folder):
        item_path = os.path.join(new_folder, item)
        if item != "book.htm":
            if os.path.isdir(item_path):
                shutil.rmtree(item_path)
            else:
                os.remove(item_path)

    # create meta.txt
    meta_path = os.path.join(new_folder, "meta.txt")
    with open(meta_path, "w", encoding="utf-8") as f:
        f.write(f"title={book['title']}\n")
        f.write(f"author={book['author']}\n")

print("Done.")
