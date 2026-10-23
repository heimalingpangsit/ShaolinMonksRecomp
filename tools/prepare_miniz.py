"""Adapt the pinned miniz ZIP64 offset check without changing downloaded sources."""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    text = args.source.read_text(encoding='utf-8')
    old = '''                if (comp_size != MZ_UINT32_MAX)
                {
                    if (((mz_uint64)MZ_READ_LE32(p + MZ_ZIP_CDH_LOCAL_HEADER_OFS) + MZ_ZIP_LOCAL_DIR_HEADER_SIZE + comp_size) > pZip->m_archive_size)'''
    new = '''                /* Local adaptation: ZIP64 stores the real offset in extra data.
                 * Extraction checks that resolved offset against the archive bounds. */
                if ((comp_size != MZ_UINT32_MAX) && (local_header_ofs != MZ_UINT32_MAX))
                {
                    if (((mz_uint64)local_header_ofs + MZ_ZIP_LOCAL_DIR_HEADER_SIZE + comp_size) > pZip->m_archive_size)'''
    if text.count(old) != 1:
        raise ValueError('The pinned miniz ZIP64 offset check changed')
    text = text.replace(old, new)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding='utf-8') != text:
        args.output.write_text(text, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
