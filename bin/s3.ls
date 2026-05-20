#!/usr/bin/env python3
"""
Public S3 Bucket Scanner
Scans for publicly accessible S3 buckets matching a pattern and lists their contents.
"""

import argparse
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed


def check_bucket(bucket_name, endpoint_url, verbose):
    """
    Check if a bucket exists and is publicly accessible, then list its contents.
    
    Args:
        bucket_name: Name of the bucket to check
        endpoint_url: S3 endpoint URL
        verbose: Whether to print verbose output
    
    Returns:
        Tuple of (bucket_name, files_list) if successful, None otherwise
    """
    cmd = [
        'aws', 's3', 'ls', '--recursive',
        f's3://{bucket_name}',
        f'--endpoint-url={endpoint_url}',
        '--no-sign-request'
    ]
    
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=120
        )
        
        if result.returncode == 0 and result.stdout.strip():
            return (bucket_name, result.stdout.strip())
        else:
            if verbose and result.stderr:
                print(f"[{bucket_name}] {result.stderr.strip()}", file=sys.stderr)
            return None
            
    except subprocess.TimeoutExpired:
        if verbose:
            print(f"[{bucket_name}] Timeout", file=sys.stderr)
        return None
    except Exception as e:
        if verbose:
            print(f"[{bucket_name}] Error: {e}", file=sys.stderr)
        return None


def generate_bucket_names(start, end, prefix='c'):
    """
    Generate bucket names with the given pattern.
    
    Args:
        start: Starting number
        end: Ending number
        prefix: Bucket name prefix
    
    Yields:
        Bucket names in the format prefixNNNNNN
    """
    for i in range(start, end + 1):
        yield f"{prefix}{i:06d}"


def main():
    parser = argparse.ArgumentParser(
        description='Scan for publicly accessible S3 buckets and list their contents',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --start 641718 --end 641720
  %(prog)s --start 0 --end 999999 --workers 20
  %(prog)s --bucket c641718 --endpoint https://c641718.parspack.net
        """
    )
    
    parser.add_argument(
        '--start',
        type=int,
        default=0,
        help='Starting number for bucket name generation (default: 0)'
    )
    
    parser.add_argument(
        '--end',
        type=int,
        default=999999,
        help='Ending number for bucket name generation (default: 999999)'
    )
    
    parser.add_argument(
        '--prefix',
        type=str,
        default='c',
        help='Bucket name prefix (default: c)'
    )
    
    parser.add_argument(
        '--bucket',
        type=str,
        help='Check a specific bucket instead of scanning a range'
    )
    
    parser.add_argument(
        '--endpoint',
        type=str,
        default='https://{bucket}.parspack.net',
        help='S3 endpoint URL. Use {bucket} as placeholder (default: https://{bucket}.parspack.net)'
    )
    
    parser.add_argument(
        '--workers',
        type=int,
        default=10,
        help='Number of concurrent workers (default: 10)'
    )
    
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='Print verbose output including errors'
    )
    
    args = parser.parse_args()
    
    # Check single bucket mode
    if args.bucket:
        endpoint = args.endpoint.replace('{bucket}', args.bucket)
        result = check_bucket(args.bucket, endpoint, args.verbose)
        if result:
            bucket_name, files = result
            print(f"\n=== Bucket: {bucket_name} ===")
            print(files)
        elif args.verbose:
            print(f"Bucket {args.bucket} is not accessible or empty", file=sys.stderr)
        return
    
    # Scan mode
    if args.verbose:
        print(f"Scanning buckets from {args.prefix}{args.start:06d} to {args.prefix}{args.end:06d}", file=sys.stderr)
        print(f"Using {args.workers} workers", file=sys.stderr)
    
    bucket_names = list(generate_bucket_names(args.start, args.end, args.prefix))
    
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = {}
        for bucket_name in bucket_names:
            endpoint = args.endpoint.replace('{bucket}', bucket_name)
            future = executor.submit(check_bucket, bucket_name, endpoint, args.verbose)
            futures[future] = bucket_name
        
        for future in as_completed(futures):
            result = future.result()
            if result:
                bucket_name, files = result
                print(f"\n=== Bucket: {bucket_name} ===")
                print(files)
                print()


if __name__ == '__main__':
    main()
