"""Convert roadmap.html to PDF with small margins using Playwright."""

import os
from playwright.sync_api import sync_playwright

HTML_PATH = os.path.join(os.path.dirname(__file__), "roadmap.html")
PDF_PATH = os.path.join(os.path.dirname(__file__), "roadmap.pdf")

with sync_playwright() as p:
    browser = p.chromium.launch()
    page = browser.new_page()
    
    # Load the HTML file
    page.goto(f"file:///{HTML_PATH.replace(os.sep, '/')}")
    
    # Wait for mermaid diagrams to render
    page.wait_for_timeout(5000)
    
    # Print to PDF with small margins
    page.pdf(
        path=PDF_PATH,
        format="A4",
        margin={
            "top": "10mm",
            "bottom": "10mm",
            "left": "10mm",
            "right": "10mm",
        },
        print_background=True,
        display_header_footer=False,
    )
    
    browser.close()

print(f"PDF written to {PDF_PATH}")
print(f"Size: {os.path.getsize(PDF_PATH) / 1024:.0f} KB")
