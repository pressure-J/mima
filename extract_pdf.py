import PyPDF2
import sys

def extract_text(pdf_path, output_path=None):
    with open(pdf_path, 'rb') as file:
        reader = PyPDF2.PdfReader(file)
        text = ''
        for page_num, page in enumerate(reader.pages):
            page_text = page.extract_text()
            if page_text:
                text += f"\n--- Page {page_num + 1} ---\n"
                text += page_text
    if output_path:
        with open(output_path, 'w', encoding='utf-8') as out_file:
            out_file.write(text)
        print(f"Text extracted to {output_path}")
    else:
        print(text)

if __name__ == "__main__":
    pdf_path = r'd:\treawork\CryptoResearch\mima\提交材料\01_论文\论文_矩阵连乘元素的逼近.pdf'
    output_path = r'd:\treawork\CryptoResearch\mima\论文文本.txt'
    extract_text(pdf_path, output_path)