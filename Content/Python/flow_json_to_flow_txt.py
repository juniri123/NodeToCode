# -*- coding: utf-8 -*-
import json
from parsed_json_to_flow import print_steps, print_common_steps, resolve_merging_points_for_entry
from restore_flow_json import restore_flow_from_json
import argparse

# --------------------------------------------------
# flow.json 또는 parsed.json 자동 처리 진입점
# --------------------------------------------------
def process(path: str, args):
    # 파일 로드
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)

    # ------------------------------------------
    # 1) flow.json 인지 판별
    # ------------------------------------------
    if not ("steps" in raw and "entry_step_key" in raw):
        raise ValueError("입력 파일은 flow.json 형식이 아닙니다.")

    steps, entry_step, common_steps, nodes = restore_flow_from_json(raw)

    # forward 출력 포맷 그대로 출력
    result = []
    result.append("=========== Steps ===========")
    result.extend(print_steps(entry_step))
    result.append("")
    result.append("")
    result.append("=========== Common Steps ===========")
    result.extend(print_common_steps(common_steps))

    if args.print_txt:
        print("\n".join(result))

    if args.save_txt:
        txt_path = args.output + ".txt"
        with open(txt_path, "w", encoding="utf-8") as f:
            f.write("\n".join(result))
        print(f"[INFO] Saved TXT → {txt_path}")

# --------------------------------------------------
# CLI 엔트리포인트 교체 버전
# --------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="UE Blueprint Flow / Flow Restore")
    ap.add_argument("--input", required=True, help="parsed.json 또는 flow.json")
    ap.add_argument("--output", required=False, help="출력 prefix (예: func_MyFunc)")
    ap.add_argument("--print-txt", action="store_true")
    ap.add_argument("--save-txt", action="store_true")
    ap.add_argument("--debug", action="store_true", help="디버그 모드 활성화")

    args = ap.parse_args()
    process(args.input, args)



if __name__ == "__main__":
    main()

#python parsed_json_to_flow.py --input func_Something_flow.json --print-txt
#python parsed_json_to_flow.py --input func_Something_flow.json --save-txt --output func_Something_restored


