﻿#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <windows.h>
#include "aviutl.hpp"
#include "exedit.hpp"
#include "detours.4.0.1/detours.h"
#pragma comment(lib, "detours.4.0.1/detours.lib")
#include "exin.hpp"

#undef min
#undef max

//
// プラグイン名です。
//
constexpr auto plugin_name = "継承元";

//
// 拡張編集にアクセスするためのオブジェクトです。
//
my::ExEditInternal exin;

//
// フック関連の処理です。
//
namespace hook
{
	//
	// 指定されたアドレスの関数をフックします。
	//
	template <typename ORIG_PROC, typename HOOK_PROC>
	BOOL attach(ORIG_PROC& orig_proc, const HOOK_PROC& hook_proc, uint32_t address)
	{
		auto exedit = (uint32_t)::GetModuleHandleA("exedit.auf");

		DetourTransactionBegin();
		DetourUpdateThread(::GetCurrentThread());

		orig_proc = (ORIG_PROC)(exedit + address);
		DetourAttach(&(PVOID&)orig_proc, hook_proc);

		DetourTransactionCommit();

		return TRUE;
	}

	//
	// 指定されたフックを解除します。
	//
	template <typename ORIG_PROC, typename HOOK_PROC>
	BOOL detach(ORIG_PROC& orig_proc, const HOOK_PROC& hook_proc)
	{
		DetourDetach(&(PVOID&)orig_proc, hook_proc);

		return TRUE;
	}
}

//
// 継承元を管理します。
//
namespace inheritance
{
	//
	// 継承元ノードです。
	//
	struct Node
	{
		ExEdit::Object* object;

		int32_t layer;
		int32_t frame_layer;
		BOOL same_group;
		BOOL no_inherit_head_filter;

		int32_t layer_range;
	};

	//
	// 継承元ノードのコレクションです。
	//
	std::vector<std::shared_ptr<Node>> collection;

	//
	// レイヤー番号をキーにしたオブジェクトのマップです。
	//
	std::unordered_map<int32_t, ExEdit::Object*> object_map;

	//
	// コレクションをリセットします。
	//
	void reset()
	{
		collection.clear();
		object_map.clear();
	}

	//
	// 継承元ノードを追加します。
	//
	void add(ExEdit::Object* object, ExEdit::Filter* filter)
	{
		auto layer = filter->track[0];
		auto frame_layer = filter->track[1] - 1;
		auto same_group = filter->check[0];
		auto no_inherit_head_filter = filter->check[1];

		auto layer_range = 100;
		if (layer) layer_range = object->layer_set + layer;

		collection.emplace_back(std::make_shared<Node>(
			object, layer, frame_layer, same_group, no_inherit_head_filter, layer_range));
	}

	//
	// 処理中オブジェクトを対象としている継承元ノードを返します。
	//
	std::shared_ptr<Node> find(ExEdit::Object* processing_object, ExEdit::FilterProcInfo* efpip)
	{
		// 継承元ノードを走査します。
		auto c = collection.size();
		for (size_t i = 0; i < c; i++)
		{
			// 継承元ノードを逆順で取得します。
			auto back_index = c - i - 1;
			const auto& node = collection[back_index];

			// 処理中オブジェクトが継承元ノードの範囲内の場合は
			if (processing_object->layer_set <= node->layer_range)
			{
				// 継承元ノードが同じグループのオブジェクトのみを対象にしている場合は
				if (node->same_group)
				{
					// 処理中オブジェクトが同じグループかチェックします。
					if (processing_object->group_belong != node->object->group_belong)
						continue;
				}

				// この継承元ノードを返します。
				return node;
			}
			// 処理中オブジェクトが継承元ノードの範囲外の場合は
			else
			{
				// これ以降の処理中オブジェクトも必ずこの継承元ノードの範囲外になります。
				// よって、この継承元ノードはこれ以降使用されないのでコレクションから取り除きます。
				collection.erase(collection.begin() + back_index);
			}
		}

		// 継承元ノードが見つからなかったのでnullptrを返します。
		return nullptr;
	}

	//
	// 指定されたレイヤーにオブジェクトを関連付けます。
	//
	void add(int32_t layer, ExEdit::Object* object)
	{
		object_map[layer] = object;
	}

	//
	// 指定されたレイヤーに存在するオブジェクトを返します。
	//
	ExEdit::Object* find(int32_t layer)
	{
		auto it = object_map.find(layer);
		if (it == object_map.end()) return nullptr;
		return it->second;
	}
}

//
// func_update()を呼び出している関数をフックします。
// 拡張編集を解析するためのコードです。実際には使用されていません。
//
namespace call_func_update
{
	//
	// この関数は拡張編集オブジェクト毎のfunc_update()です。
	//
	void (CDECL* orig_proc)(ExEdit::Object* processing_object, uint32_t u2, ExEdit::Object* processing_object2, ExEdit::FilterProcInfo* efpip) = nullptr;
	void CDECL hook_proc(ExEdit::Object* processing_object, uint32_t u2, ExEdit::Object* processing_object2, ExEdit::FilterProcInfo* efpip)
	{
		inheritance::add(processing_object->layer_set, processing_object);

		return orig_proc(processing_object, u2, processing_object2, efpip);
	}

	//
	// 初期化処理です。
	// フックをセットします。
	//
	BOOL init()
	{
		return hook::attach(orig_proc, hook_proc, 0x4A290);
	}

	//
	// 後始末処理です。
	// フックを解除します。
	//
	BOOL exit()
	{
		return hook::detach(orig_proc, hook_proc);
	}
}

//
// func_proc()を呼び出している関数をフックします。
//
namespace call_func_proc
{
	//
	// 指定されたオブジェクトが持つ最後のフィルタを返します。
	//
	int32_t find_last_filter_index(ExEdit::Object* object)
	{
		for (int32_t i = 0; i < ExEdit::Object::MAX_FILTER; i++)
		{
			auto back_index = ExEdit::Object::MAX_FILTER - i - 1;

			if (object->filter_param[back_index].is_valid())
				return back_index;
		}

		return ExEdit::Object::FilterParam::None;
	}

	//
	// 指定されたオブジェクトが持つ継承元フィルタを返します。
	//
	ExEdit::Filter* find_inheritance_filter(ExEdit::Object* object)
	{
		for (int32_t i = 0; i < ExEdit::Object::MAX_FILTER; i++)
		{
			auto filter = exin.get_filter(object, i);
			if (!filter) return nullptr;
			if (!filter->name) continue;
			if (strcmp(filter->name, plugin_name)) continue;
			if (!(object->filter_status[i] & ExEdit::Object::FilterStatus::Active)) continue;

			return filter;
		}

		return nullptr;
	}

	//
	// 指定されたオブジェクトが持つテキストフィルタを返します。
	//
	ExEdit::Filter* find_text_filter(ExEdit::Object* object)
	{
		// テキストフィルタのインデックスです。
		constexpr int32_t text_filter_index = 0;

		auto filter = exin.get_filter(object, text_filter_index);
		if (!filter) return nullptr;
		if (!filter->name) return nullptr;
		if (strcmp(filter->name, "テキスト")) return nullptr;

		return filter;
	}

	//
	// テキストオブジェクトの拡張データを返します。
	//
	auto get_text_exdata(ExEdit::Object* object)
	{
		// テキストフィルタのインデックスです。
		constexpr int32_t text_filter_index = 0;

		return (ExEdit::Exdata::efText*)exin.get_exdata(object, text_filter_index);
	}

	//
	// テキストオブジェクトのテキストを返します。
	//
	std::vector<wchar_t> get_text(ExEdit::Object* object)
	{
		auto exdata = get_text_exdata(object);
		return { std::begin(exdata->text), std::end(exdata->text) };
	}

	//
	// テキストオブジェクトのテキストを変更します。
	//
	void set_text(ExEdit::Object* object, const std::vector<wchar_t>& text)
	{
		auto exdata = get_text_exdata(object);
		std::copy(std::begin(text), std::end(text), exdata->text);
	}

	//
	// この構造体は継承元オブジェクトの設定を
	// 一時的に書き換えて処理中オブジェクトに偽装させます。
	//
	struct Disguiser
	{
		//
		// この構造体はフィルタ設定値へのアクセサです。
		//
		struct FilterAcc
		{
			ExEdit::Object* object;
			int32_t filter_index;
			ExEdit::Filter* filter;

			//
			// コンストラクタです。
			//
			FilterAcc(ExEdit::Object* object, int32_t filter_index)
				: object(object)
				, filter_index(filter_index)
				, filter(exin.get_filter(object, filter_index))
			{
			}

			//
			// アクセサが有効の場合はTRUEを返します。
			//
			bool is_valid() const { return !!filter; }

			//
			// 左トラックの参照を返します。
			//
			int32_t& track_left(int32_t index)
			{
				return object->track_value_left[object->filter_param[filter_index].track_begin + index];
			}

			//
			// 右トラックの参照を返します。
			//
			int32_t& track_right(int32_t index)
			{
				return object->track_value_right[object->filter_param[filter_index].track_begin + index];
			}

			//
			// チェックの参照を返します。
			//
			int32_t& check(int32_t index)
			{
				return object->check_value[object->filter_param[filter_index].check_begin + index];
			}
		};

		//
		// この構造体は変更対象のオブジェクトの値です。
		//
		struct ObjectSettings
		{
			std::vector<wchar_t> text;
			int32_t frame_begin = 0;
			int32_t frame_end = 0;
			int32_t layer_disp = 0;
			int32_t layer_set = 0;
			int32_t scene_set = 0;

			// 標準描画の場合 track_n = 6, check_n = 1, pos[3], scale, alpha, angle;
			// 拡張描画の場合 track_n = 12, check_n = 2, pos[3], scale, alpha, aspect, angle[3], origin[3];
			struct LastFilter {
				std::unique_ptr<FilterAcc> acc;
				struct Track {
					int32_t pos[3] = {};
					int32_t scale = {};
					int32_t alpha = {};
					int32_t aspect = {};
					int32_t angle[3] = {};
					int32_t origin[3] = {};
				} track_left, track_right;
				int32_t check[2] = {};

				//
				// 指定されたオブジェクトから値を読み込みます。
				//
				void read(ExEdit::Object* object)
				{
					acc = std::make_unique<FilterAcc>(object, find_last_filter_index(object));

					if (strcmp(acc->filter->name, "標準描画") == 0)
					{
						track_left.pos[0] = acc->track_left(0);
						track_left.pos[1] = acc->track_left(1);
						track_left.pos[2] = acc->track_left(2);
						track_left.scale = acc->track_left(3);
						track_left.alpha = acc->track_left(4);
						track_left.angle[2] = acc->track_left(5);
						track_right.pos[0] = acc->track_right(0);
						track_right.pos[1] = acc->track_right(1);
						track_right.pos[2] = acc->track_right(2);
						track_right.scale = acc->track_right(3);
						track_right.alpha = acc->track_right(4);
						track_right.angle[2] = acc->track_right(5);
						check[0] = acc->check(0);
					}
					else if (strcmp(acc->filter->name, "拡張描画") == 0)
					{
						track_left.pos[0] = acc->track_left(0);
						track_left.pos[1] = acc->track_left(1);
						track_left.pos[2] = acc->track_left(2);
						track_left.scale = acc->track_left(3);
						track_left.alpha = acc->track_left(4);
						track_left.aspect = acc->track_left(5);
						track_left.angle[0] = acc->track_left(6);
						track_left.angle[1] = acc->track_left(7);
						track_left.angle[2] = acc->track_left(8);
						track_left.origin[0] = acc->track_left(9);
						track_left.origin[1] = acc->track_left(10);
						track_left.origin[2] = acc->track_left(11);
						track_right.pos[0] = acc->track_right(0);
						track_right.pos[1] = acc->track_right(1);
						track_right.pos[2] = acc->track_right(2);
						track_right.scale = acc->track_right(3);
						track_right.alpha = acc->track_right(4);
						track_right.aspect = acc->track_right(5);
						track_right.angle[0] = acc->track_right(6);
						track_right.angle[1] = acc->track_right(7);
						track_right.angle[2] = acc->track_right(8);
						track_right.origin[0] = acc->track_right(9);
						track_right.origin[1] = acc->track_right(10);
						track_right.origin[2] = acc->track_right(11);
						check[0] = acc->check(0);
						check[1] = acc->check(1);
					}
					else
					{
						acc.reset();
					}
				}

				//
				// 指定されたオブジェクトに値を書き込みます。
				//
				void write(ExEdit::Object* object)
				{
					if (!acc) return;

					auto acc = std::make_unique<FilterAcc>(object, find_last_filter_index(object));

					if (strcmp(acc->filter->name, "標準描画") == 0)
					{
						acc->track_left(0) = track_left.pos[0];
						acc->track_left(1) = track_left.pos[1];
						acc->track_left(2) = track_left.pos[2];
						acc->track_left(3) = track_left.scale;
						acc->track_left(4) = track_left.alpha;
						acc->track_left(5) = track_left.angle[2];
						acc->track_right(0) = track_right.pos[0];
						acc->track_right(1) = track_right.pos[1];
						acc->track_right(2) = track_right.pos[2];
						acc->track_right(3) = track_right.scale;
						acc->track_right(4) = track_right.alpha;
						acc->track_right(5) = track_right.angle[2];
						acc->check(0) = check[0];
					}
					else if (strcmp(acc->filter->name, "拡張描画") == 0)
					{
						acc->track_left(0) = track_left.pos[0];
						acc->track_left(1) = track_left.pos[1];
						acc->track_left(2) = track_left.pos[2];
						acc->track_left(3) = track_left.scale;
						acc->track_left(4) = track_left.alpha;
						acc->track_left(5) = track_left.aspect;
						acc->track_left(6) = track_left.angle[0];
						acc->track_left(7) = track_left.angle[1];
						acc->track_left(8) = track_left.angle[2];
						acc->track_left(9) = track_left.origin[0];
						acc->track_left(10) = track_left.origin[1];
						acc->track_left(11) = track_left.origin[2];
						acc->track_right(0) = track_right.pos[0];
						acc->track_right(1) = track_right.pos[1];
						acc->track_right(2) = track_right.pos[2];
						acc->track_right(3) = track_right.scale;
						acc->track_right(4) = track_right.alpha;
						acc->track_right(5) = track_right.aspect;
						acc->track_right(6) = track_right.angle[0];
						acc->track_right(7) = track_right.angle[1];
						acc->track_right(8) = track_right.angle[2];
						acc->track_right(9) = track_right.origin[0];
						acc->track_right(10) = track_right.origin[1];
						acc->track_right(11) = track_right.origin[2];
						acc->check(0) = check[0];
						acc->check(1) = check[1];
					}
				}
			} last_filter;

			//
			// コンストラクタです。
			//
			ObjectSettings(ExEdit::Object* object, BOOL no_inherit_head_filter, ExEdit::Object* frame_object = nullptr)
			{
				text = get_text(object);
				frame_begin = object->frame_begin;
				frame_end = object->frame_end;
				layer_disp = object->layer_disp;
				layer_set = object->layer_set;
				scene_set = object->scene_set;

				if (no_inherit_head_filter)
					last_filter.read(object);

				if (frame_object)
				{
					frame_begin = frame_object->frame_begin;
					frame_end = frame_object->frame_end;
				}
			}

			//
			// 指定されたオブジェクトに設定値を適用します。
			//
			void apply(ExEdit::Object* object)
			{
				set_text(object, text);
				object->frame_begin = frame_begin;
				object->frame_end = frame_end;
				object->layer_disp = layer_disp;
				object->layer_set = layer_set;
				object->scene_set = scene_set;

				last_filter.write(object);
			}
		};

		ExEdit::Object* processing_object = nullptr;
		std::shared_ptr<inheritance::Node> node;
		std::unique_ptr<ObjectSettings> inheritance_settings;

		//
		// コンストラクタです。
		// 継承元オブジェクトを書き換えます。
		//
		Disguiser(ExEdit::Object* processing_object, const std::shared_ptr<inheritance::Node>& node)
			: processing_object(processing_object)
			, node(node)
		{
			// あとで元に戻せるように
			// 継承元オブジェクトの設定値を保存しておきます。
			inheritance_settings = std::make_unique<ObjectSettings>(node->object, node->no_inherit_head_filter);

			auto frame_object = inheritance::find(node->frame_layer);

			// 処理中オブジェクトの設定値を取得します。
			auto processing_settings = std::make_unique<ObjectSettings>(
				processing_object, node->no_inherit_head_filter, frame_object);

			// 継承元オブジェクトの設定値を書き換えます。
			processing_settings->apply(node->object);
		}

		//
		// デストラクタです。
		// 継承元オブジェクトの設定を元に戻します。
		//
		~Disguiser()
		{
			// 継承元オブジェクトの設定を元に戻します。
			inheritance_settings->apply(node->object);
		}
	};

	//
	// この関数は拡張編集オブジェクト毎のfunc_proc()です。
	//
	void (CDECL* orig_proc)(ExEdit::Object* processing_object, ExEdit::FilterProcInfo* efpip, uint32_t flags) = nullptr;
	void CDECL hook_proc(ExEdit::Object* processing_object, ExEdit::FilterProcInfo* efpip, uint32_t flags)
	{
		auto filter = exin.get_filter(processing_object, 0);

		// フラグが立っている場合は
		if (flags)
		{
			// 個別オブジェクトなどなのでデフォルト処理を実行します。
			return orig_proc(processing_object, efpip, flags);
		}

		// このレイヤーとオブジェクトを関連付けます。
		inheritance::add(processing_object->layer_set, processing_object);

		// 処理中オブジェクトに継承元フィルタを持つ場合は
		if (auto filter = find_inheritance_filter(processing_object))
		{
			// 継承元ノードをコレクションに追加します。
			inheritance::add(processing_object, filter);

			// このオブジェクトは処理しないようにします。
			return;
		}

		// 処理中オブジェクトがテキストフィルタを持つ場合は
		if (find_text_filter(processing_object))
		{
			// 処理中オブジェクトを対象としている継承元ノードを取得します。
			if (const auto& node = inheritance::find(processing_object, efpip))
			{
				// オブジェクトを偽装します。
				Disguiser disguiser(processing_object, node);

				// 偽装したオブジェクトを拡張編集に渡して処理させます。
				return orig_proc(node->object, efpip, flags);
			}
		}

		// デフォルト処理を実行します。
		return orig_proc(processing_object, efpip, flags);
	}

	//
	// 初期化処理です。
	// フックをセットします。
	//
	BOOL init()
	{
		return hook::attach(orig_proc, hook_proc, 0x49370);
	}

	//
	// 後始末処理です。
	// フックを解除します。
	//
	BOOL exit()
	{
		return hook::detach(orig_proc, hook_proc);
	}
}

//
// この構造体は登録するフィルタ情報を管理します。
//
inline struct {
	inline static constexpr int32_t filter_n = 1;

	//
	// この構造体は設定共通化フィルタです。
	//
	struct Inheritance {
		//
		// トラックの定義です。
		//
		inline static constexpr int32_t track_n = 2;
		struct Track {
			LPCSTR name;
			int32_t default_value;
			int32_t min_value;
			int32_t max_value;
			int32_t scale;
		} track[track_n] = {
			{ "ﾚｲﾔｰ数", 1, 0, 100 },
			{ "ﾌﾚｰﾑﾚｲﾔｰ", 0, 0, 100 },
		};
		LPCSTR track_name[track_n] = {};
		int32_t track_default[track_n] = {};
		int32_t track_s[track_n] = {};
		int32_t track_e[track_n] = {};

		//
		// チェックの定義です。
		//
		inline static constexpr int32_t check_n = 2;
		struct Check {
			LPCSTR name;
			int32_t default_value;
		} check[check_n] = {
			{ "同じグループのオブジェクトを対象にする", FALSE },
			{ "標準描画・拡張描画は継承しない", FALSE },
		};
		LPCSTR check_name[check_n] = {};
		int32_t check_default[check_n] = {};

		//
		// フィルタの定義です。
		//
		ExEdit::Filter filter = {
			.flag = ExEdit::Filter::Flag::Effect,
			.name = plugin_name,
			.track_n = track_n,
			.track_name = const_cast<char**>(track_name),
			.track_default = track_default,
			.track_s = track_s,
			.track_e = track_e,
			.check_n = check_n,
			.check_name = const_cast<char**>(check_name),
			.check_default = check_default,
			.func_proc = func_proc,
			.func_init = func_init,
			.func_update = func_update,
		};

		//
		// コンストラクタです。
		//
		Inheritance()
		{
			// トラックデータを個別の配列に変換します。
			init_array(track_name, track, &Track::name);
			init_array(track_default, track, &Track::default_value);
			init_array(track_s, track, &Track::min_value);
			init_array(track_e, track, &Track::max_value);

			// チェックデータを個別の配列に変換します。
			init_array(check_name, check, &Check::name);
			init_array(check_default, check, &Check::default_value);
		}

		template <typename T0, size_t N, typename T1, typename T2>
		inline static void init_array(T0 (&t0)[N], const T1& t1, T2 t2)
		{
			for (size_t i = 0; i < N; i++)
				t0[i] = t1[i].*t2;
		}

		inline static BOOL func_init(ExEdit::Filter* efp)
		{
			exin.init();
//			call_func_update::init();
			call_func_proc::init();

			return TRUE;
		}

		inline static BOOL func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip)
		{
			return TRUE;
		}

		inline static BOOL func_update(ExEdit::Filter* efp, int32_t status)
		{
			switch (status)
			{
			case 1: // pre_workflow
			case 2: // post_workflow
				{
					// 継承元をリセットします。
					// (ここは拡張編集の描画ワークフローの最初と最後に呼び出されます)
					inheritance::reset();

					break;
				}
			case 3: // pre_func_proc
				{
					// ここでTRUEを返すと、このフィルタが付与されている
					// オブジェクトが描画されなくなります。(func_proc()が呼ばれなくなります)
					break;
				}
			}

			return FALSE;
		}
	} inheritance;

	ExEdit::Filter* filter_list[filter_n + 1] = {
		&inheritance.filter,
		nullptr,
	};
} registrar;

//
// フィルタ登録関数です。
// エクスポート関数です。
// patch.aulから呼び出されます。
//
EXTERN_C ExEdit::Filter** WINAPI GetFilterTableList()
{
	return registrar.filter_list;
}
