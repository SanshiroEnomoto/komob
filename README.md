<img src="Komob.png" width="10%">
<span style="font-size:200%">Komob (小モブ): 小型 Modbus サーバー</span>


Komob is a small and lightweight Modbus/TCP server library written in C++.<br>
(For English documentation, see [README_EN.md](README_EN.md))

# 概要
## 特徴

**Komob** は，組み込み機器などを Modbus/TCP で制御できるようにするための，C++ 製・超軽量 Modbus サーバーです．
FPGA + Linux (SoC) 環境での利用を想定して設計されており，依存関係を極力排除し，ユーザコードにそのまま組み込めることを重視しています．

- **C++ で書かれた軽量 Modbus サーバー**
  - SoC 上で動作するデバイスを Modbus/TCP 経由で制御可能
  - 組み込み Linux やリソース制約のある環境向け

- **ヘッダオンリー，自己完結**
  - 外部依存なし（標準C++ライブラリだけで構成）
  - `#include "modbus.hpp"` するだけで使用可能
  - ライブラリのビルドやリンクは不要，CMake 等のビルドシステムへの依存なし

- **16-bit / 32-bit レジスタ対応**
  - Modbus の 16-bit ワードをふたつまとめて 32-bit として扱う

- **複数クライアントの同時接続に対応**
  - ポーリングループで実装し，スレッドを使用しない設計
  - レジスタアクセスは直列化され，同時実行は原理的に起きない
  - 低リソース環境でも安定した動作を重視

- **複数のレジスタハンドラを登録可能**
  - 複数のレジスタテーブルを登録できる
  - Chain-of-Responsibility パターンで処理される
  - アドレス空間を論理的に分割した実装が可能
  - ソフトウェア仮想レジスタやアクセスモニタなどが可能


## 想定用途

- FPGA / SoC ベースの制御デバイス
- 実験装置・計測機器の Modbus インターフェース
- 軽量な組み込み向け Modbus/TCP サーバー
- PLC や SCADA との接続用デバイス


## 設計方針

- シンプルさを最優先した実装
- 長期連続運転を想定
- SoC / FPGA フレンドリーなレジスタモデル
- 拡張しやすい構造（Chain-of-Responsibility）


# 使い方
## ユーザレジスタテーブル
クライアントは，Modbus プロトコルを介して，アドレスで指定されるレジスタ (Modbus Holding Registers) に対して整数値（16bit または 32bit）の読み書きを行います．
ユーザのレジスタテーブルを定義して，そこでユーザデバイス用のレジスタの読み書きを実装します．
手順は以下のとおりです：

1. `komob::RegisterTable` を継承して自分のレジスタテーブルを作成
2. そこに読み書きメソッドを実装：
  - 以下のメソッドを実装する
    - **`bool read(unsigned address, unsigned& value)`**: `address` のレジスタを読み値を `value` に代入
    - **`bool write(unsigned address, unsigned value)`**: `address` のレジスタに値 `value` を書き込む
  - 引数の `address` が正当なら `true` を，そうでなければ `false` を返す
  - エラーの場合は何かを例外として投げる（何を投げてもクライアントに SLAVE_FAILURE レスポンスが戻るだけ．ロギングは自分で．）

### レジスタテーブルの実装例
以下は，書かれた値を記憶するメモリレジスタ 256 個をソフトウェア実装した例です．

```cpp
#include <iostream>
#include "komob.hpp"

class MemoryRegisterTable : public komob::RegisterTable {
  public:
    MemoryRegisterTable(unsigned size = 256): registers(size, 0) {}

    bool read(unsigned address, unsigned & value) override {
        if (address >= registers.size()) {
            return false;
        }
        value = registers[address];
        return true;
    }
<
    bool write(unsigned address, unsigned value) override {
        if (address >= registers.size()) {
            return false;
        }
        registers[address] = value;
        return true;
    }

  private:
    std::vector<unsigned> registers;
};
```

ロギングが必要な場合は，とりあえず `std::cerr` に出力し，以下のサーバーの走らせ方に応じて，適当なログ収集システムに接続することを想定しています．

## サーバー部分
サーバーは，502 もしくは指定されたポートを開き，接続してきたクライアントに対し，Modbus プロトコルでユーザのレジスタテーブルを読み書きできるようにします．

### 標準構成
作成したレジスタテーブルのインスタンスをひとつ作成し，その `shared_ptr` をサーバーに渡します．レジスタテーブルと同じファイル内に記述して構いません．
```cpp
int main(int argc, char** argv)
{
    return komob::Server(
        std::make_shared<MemoryRegisterTable>()
    ).run(argc, argv);
}
```
サーバーのデフォルト設定は以下のようになっています：

|パラメータ|デフォルト|コメント|
|--|--|--|
|ポート番号| 502 |プログラムの第１パラメータ(`argv[1]`)で変更可能 |
|同時接続数| 制限なし | |
|Keepalive Idle| 3600 秒 | 自動接続切断までの無通信状態の継続時間 |
|Timeout |1000 ミリ秒 | 途中で切れた Modbus パケットの最大待ち時間 |

タイムアウトが起きた場合，サーバーは接続を切断します．
クライアントは，継続処理をしたい場合は，再接続をしてください．

### 複数のレジスタテーブルを使う
詳細は Chain-of-Responsibility の説明を参照してください．

```cpp
int main(int argc, char** argv)
{
    return (komob::Server()
        .add(std::make_shared<MyRegisterTable1>())
        .add(std::make_shared<MyRegisterTable2>())
    ).run(argc, argv);
}
```

### 16bit モード
Modbus 自体は 16bit プロトコルですが，Komob はデフォルトでデータワードを２つまとめて 32bit アクセスをします．
PLC などとの互換性のために，オプションで 16bit アクセスを使用することもできます．

```cpp
int main(int argc, char** argv)
{
    return komob::Server(
        std::make_shared<MemoryRegisterTable>(),
        komob::DataWidth::W16
    ).run(argc, argv);
}
```

## コンパイルと起動
Komob は単一のヘッダファイルだけで構成されているので，ライブラリをリンクする必要も，特別なビルドツールを使う必要もありません．
レジスタテーブルと上記 `main()` を書いたファイルが `my-modbus-server.cpp` というファイル名なら，`komob.hpp` ファイルを同じディレクトリにコピーし，以下のようにコンパイルできます：
```
g++ -o my-modbus-server my-modbus-server.cpp
```
（あるいは，`komob.hpp` ファイルをコピーする代わりに，その場所を `-I` オプションで指定しても構いません．）

そのまま実行すればボートを開き，クライアントからの接続を待ちます（複数接続可）．
```
./my-modbus-server
```
ポート番号を変更するためには，プログラムパラメータで指定してください．
```
./my-modbus-server 1502
```

通常は，システム開始時に自動実行するように設定すると思います．以下のような方法が多く使われます：

- ちゃんとした方法
  - systemd サービスに登録する (ログは Journal または syslog に入る)  
- 状況が許すなら（SoC ではたぶん無理）
  - Docker / Docker Compose で運用する （ログを柔軟にリダイレクトできる．Elasticsearch とか）
- 手軽な方法
  - `/etc/rc.local` に書く
  - `crontab` の `@reboot` に書く
- 一時的な方法
  - tmux / screen の中で走らせる

具体的な方法については，AI に，「`/PATH/TO/CODE/my-modbus-server` というコマンドを systemd を使ってシステム起動時に自動で実行するようにしたい」と言えばやり方を教えてくれます．

## クライアント側
16bit モードでは，通常の Modbus クライアントがそのまま使えます．
サーバーが 32bit モード（デフォルト）であっても，上位 16bit が全て 0 で，複数レジスタの同時読み書きをしない場合であれば，同様です．
Input Register や Coil ではなく，Holding Register にアクセスしてください．
以下は，PyModbus を使って一つのレジスタの 16bit 値を読み書きする例です．

```python
host, port = '192.168.50.63', 502

from pymodbus.client import ModbusTcpClient
client = ModbusTcpClient(host, port=port)

address = 0x10
value = 0xabcd

# Writing a 16bit value to the device
reply = client.write_registers(address, [value])
if reply is None or reply.isError():
    print("ERROR")

# Reading a 16bit value from the device
reply = client.read_holding_registers(address, count=1)
if reply is None or reply.isError():
    print("ERROR")
else:
    print(hex(reply.registers[0]))
```

32bit 値を使う場合は次の章を参照してください．


# 32bit データの扱い
Modbus のデータ幅は 16bit で，32bit データのアクセスは仕様に規定されていません．
Komob では 16bit を２つ組み合わせて 32bit データと解釈する 32bit モードを選択できます（デフォルト）．
実装の詳細に興味がなければ，最後のクライアント例まで読み飛ばして構いません．

## 設計
### 32bit モードにおける Modbus の adress と quantity の解釈

Modbus プロトコル上の `quantity` は，規格どおり「16bit ワード数（データサイズ）」として扱います．32bit モードでは，`quantity` パラメータはデータブロックの大きさを示すものとして扱い，データブロックを 32bit データの配列と読み替えます．
したがって，32bit モードでは：

- `adress` はレジスタテーブルの address に直接対応します（奇数を含む）
- `quantity` は常に偶数である必要があります．奇数の `quantity` を指定した要求はエラーとなります
- 32bit を超えるデータブロックは，連続するレジスタのデータ配列と解釈されます

この方式により，RegisterTable は Modbus 固有の「16bit ワード」や「偶数アドレス制約」を意識する必要がありません．

#### サーバーモードごとのマッピング例
Modbus アドレス `1000` に `[ 0x1111, 0x2222, 0x3333, 0x4444 ]` を書き込んだ場合， Modbus パケットの `quantity` (ワード数) パラメータの値は `4` で，これをサーバーが受け取ったとき， `RegisterTable` の `write(address, value)` は以下のように呼び出されます：

| address | value, 16bitモード | value, 32bitモード | value, 32bit CDAB モード |
|--|--|--|--|
| 1000 | 0x1111 | 0x11112222 | 0x22221111 |
| 1001 | 0x2222 | 0x33334444 | 0x44443333 |
| 1002 | 0x3333 | - | - |
| 1003 | 0x4444 | - | - |

つまり，同じサイズのデータブロックに対して，16bit モードの場合，`write()` は４回呼び出され，32bit モードの場合は２回呼び出されます．
Multi-Register アクセスの場合，モードによってレジスタアドレスが変わることに注意してください．
16bit/32bit モードの選択はシステム設計時に固定されるという想定で，実行時に変わるまたは運用中に切り替えるということは想定されていません．（切り替えや混在が予想される場合は，Multi-Register アクセスを行わないという選択肢もあります．）

Read の場合も同様で，アドレス `1000` から `quantity`/`count` を `4` として読み出し要求をすると，上記のように `read(address, &value)` が４回 (16bit) または２回 (32bit) 呼ばれ，クライアントには長さ `4` の配列が戻されます．したがって，32bit モードでは，クライアント側での再構築が必要となります（以下のクライアント実装例を参照）．

### プロトコル独立な Register Table

Komob における RegisterTable は，特定の通信プロトコルに依存しない「論理レジスタ配列」を表現するための抽象です．

- RegisterTable は 整数インデックス → 値 の写像として振る舞う
- インデックスは，デバイス仕様書に記載される 論理レジスタ番号
- RegisterTable 自身は データ幅を規定しない
- `unsigned` 型は「幅未定の値コンテナ」として使用している

この設計により，同一の RegisterTable 実装を

- Modbus
- メモリマップド I/O
- SPI / I2C
- 将来の別プロトコル

など，複数のアクセス手段から再利用できます．

### データ幅は通信サーバーのビュー

実際に値を 何ビットとして外部に公開するかは，RegisterTable ではなくServer の動作モードによって決定されます．
Komob では，Server の動作モードとして 16bit / 32bit を明示的に切り替えられます．

- **16bit モード**
  - 1 論理レジスタ = 16bit
  - Modbus では 1 ワードとして転送
- **32bit モード**
  - 1 論理レジスタ = 32bit
  - Modbus では 2 ワード（16bit × 2）として転送

Server は RegisterTable から取得した値に対して，

- 指定されたビット幅でマスク（切り詰め）
- 必要に応じてワード分割・結合

を行います．指定幅を超える値はオーバーフローとして切り捨てられます．これは FPGA / SoC 環境では一般的な挙動であり，エラーとはしません．

## 運用想定

### SoC 環境: 32bit が便利

SoC / FPGA 上での利用では，以下の理由により，32bit モードが事実上の標準運用となることを想定しています．

- SoC 内部レジスタや MMIO は 32bit 幅であることが多い
- 論理レジスタ番号と実装が自然に一致する
- Modbus は単なる 外部インターフェース（ビュー）として機能する


### 既存 PLC / SCADA システムとの統合: 16bit が安全

既存の PLC や SCADA，HMI などのシステムと Modbus で統合する場合は，16bit モードでの運用が安全です．多くの PLC システムは，Modbus の Holding Register を 16bit レジスタとして扱うことを前提としており，32bit 値はベンダ固有の方法で 2 レジスタを束ねて表現しています．そのため，32bit モードを使用すると，以下のような要因により，相互運用性の問題が生じやすくなります．

- ワード順（ABCD / CDAB など）の不一致
- アドレス境界やアラインメントの違い
- PLC ベンダごとの型定義の差異


## 32bit モードでのクライアント実装

Modbus クライアント側では，通常のライブラリ（例：pymodbus）を使用し，

- 32bit 値 1 個につき 16bit レジスタを 2 個まとめて read/write
- デフォルトで，上位ワードが先 (Big Endian, "ABCD" ワードアラインメント)
- データサイズは 16bit ワードの数
- ただし，アドレスは，32bit ごとに１づつ増える (RegisterTable は 32bit 配列)

という追加規約でアクセスできます．

### pymodbus の例
32bit アクセスでは，書き込みで 16bit 配列に分解，読み出しで２ワードごとの結合を行ってください．

```python
def write32(client, address, value):
    data = [ (value >> 16) & 0xffff,  value & 0xffff ]
    reply = client.write_registers(address, data)
    return (reply is not None) and (not reply.isError())
    
def read32(client, address):
    reply = client.read_holding_registers(address, count=2)
    if reply is None or reply.isError():
        return None
    return ((reply.registers[0] & 0xffff) << 16) | (reply.registers[1] & 0xffff)

###

host, port = '192.168.50.63', 502

from pymodbus.client import ModbusTcpClient
client = ModbusTcpClient(host, port=port)

address = 0x10
write32(client, address, 0x12345678)

import time
while True:
    value = read32(client, address)
    print(hex(value))

    write32(client, address, value + 1)
    
    time.sleep(1)
```

### SlowPy の例
[SlowDash](https://github.com/slowproj/slowdash) の Python ライブラリ SlowPy を使えば，32bit アクセスを直接使えます．

```python
host, port = '192.168.50.63', 502

from slowpy.control import control_system as ctrl
modbus = ctrl.import_control_module('Modbus').modbus(host, port)

reg = modbus.register32(0x10)
reg.set(0x12345678)

import time
while True:
    value = reg.get()
    print(hex(value))
    
    reg.set(value + 1)
    
    time.sleep(1)
```


# Chain-of-Responsibility による複数レジスタテーブルの使用
## 構造

Komob では，複数の `RegisterTable` をチェイン（鎖）のように並べて登録できます．

```cpp
int main(int argc, char** argv)
{
    return (komob::Server()
        .add(std::make_shared<MyRegisterTable1>())
        .add(std::make_shared<MyRegisterTable2>())
    ).run(argc, argv);
}
```

リクエスト（read/write）は先頭のテーブルから順に渡され，どれか1つが処理に成功した時点で確定します．

- `read()` / `write()` が true を返した時点で処理を確定
- false を返した場合は，次の `RegisterTable` に処理を委譲

この仕組みにより，単に「機能ごとに分割したレジスタテーブルを作る」だけでなく，機能を付加する“ソフトウェアレジスタ”を後付けしたり，アクセス監視のような横断的機能を追加できます．


## できることの例

- **機能分割**：アドレス範囲や用途ごとにテーブルを分けて見通しを良くする
- **後付け機能（ソフトウェアレジスタ）**：読み出し回数・最終アクセス時刻・エラーカウンタ等を“仮想レジスタ”として提供する
- **監視・計測**：アクセスログ，レート計測，特定アドレスのトレースなどを既存実装に手を入れず追加する
- **ガード／フィルタ**：書き込み禁止領域の拒否，アドレス範囲のホワイトリスト化，値のクランプ等

このパターンを前提にすると，最初は最小の実装で始めて，必要になった機能を段階的に積み増す設計がしやすくなります．

### 1. 機能ごとに分割したレジスタテーブル

典型的な使い方は，機能単位でレジスタテーブルを分割する方法です．

- ステータスレジスタ
- 設定レジスタ
- 制御レジスタ
- デバッグ用レジスタ

それぞれを独立した `RegisterTable` として実装し，サーバーに順番に登録することで，可読性と保守性を高められます．

### 2. ソフトウェア的な機能を付加するレジスタ

Chain-of-Responsibility の利点は，「実体を持たないソフトウェアレジスタ」を自然に追加できる点です．

例えば，

- 仮想レジスタ
- 計算結果を返すレジスタ
- 他のレジスタ操作をトリガーするレジスタ

などを，既存のレジスタマップを変更せずに追加できます．

### 3. アクセスモニタとしての利用例

以下は，Modbus の read / write アクセスを監視するためのモニタ専用 RegisterTable の例です．

```cpp
class RequestMonitor : public komob::RegisterTable {
  public:
    bool read(unsigned address, unsigned & value) override {
        std::cout << "ModbusRead(" << std::hex << address << ")" << std::dec << std::endl;
        return false;  // 次の RegisterTable に処理を委譲
    }

    bool write(unsigned address, unsigned value) override {
        std::cout << "ModbusWrite(" << std::hex << address << ", " << value << ")" << std::dec << std::endl;
        return false;  // 次の RegisterTable に処理を委譲
    }
};
```

このような `RegisterTable` をチェインの先頭に置くことで，

- すべての Modbus アクセスをログに記録
- アクセス頻度や利用状況の監視
- デバッグ用途のトレース

といった用途に利用できます．重要なのは，このモニタが実際のレジスタ処理を一切変更しない点です．

### 4. レイヤー構造としての RegisterTable

Chain-of-Responsibility を用いることで，RegisterTable は以下のような「レイヤー構造」として設計できます．

- 監視・ロギング層
- 仮想／補助レジスタ層
- 実レジスタ層（SoC / FPGA 直結）

それぞれが独立しており，必要に応じて追加・削除・並び替えが可能です．
