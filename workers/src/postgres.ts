import { Client, DatabaseError } from 'pg';
import type { QueryResult } from 'pg';

type Result = { readonly rows: readonly (readonly (string | null)[])[]; readonly affected: number; readonly state: string };
export class Postgres {
  private readonly params = new Map<bigint, (string | null)[]>();
  private readonly results = new Map<bigint, Result>();
  private next = 1n;
  constructor(private readonly client: Client) {}
  private async query(sql: string, values: readonly (string | null)[] = []): Promise<bigint> {
    const handle = this.next++;
    try {
      // libpq exposes text values, including PostgreSQL booleans as t/f and JSON as serialized text.
      const result: QueryResult<(string | null)[]> = await this.client.query({
        text: sql, values: [...values], rowMode: 'array',
        types: { getTypeParser: (_oid: number) => (value: string) => value },
      });
      this.results.set(handle, { rows: result.rows, affected: result.rowCount ?? 0, state: '' });
    } catch (error) {
      if (!(error instanceof DatabaseError)) throw error;
      console.error(JSON.stringify({ event: 'postgres.query.failed', state: error.code ?? 'XX000' }));
      this.results.set(handle, { rows: [], affected: 0, state: error.code ?? 'XX000' });
    }
    return handle;
  }
  async close(): Promise<void> { await this.client.end(); }
  readonly native = {
    platform_pool: () => 1n,
    fpg_acquire: async (_pool: bigint) => {
      try { await this.client.connect(); return 1n; }
      catch (error) {
        if (!(error instanceof Error)) throw error;
        console.error(JSON.stringify({ event: 'postgres.connect.failed' }));
        return 0n;
      }
    },
    fpg_release: (_pool: bigint, _conn: bigint) => 1n,
    fpg_params: () => { const handle = this.next++; this.params.set(handle, []); return handle; },
    fpg_push: (handle: bigint, value: string) => { this.params.get(handle)?.push(value); return 1n; },
    fpg_push_null: (handle: bigint) => { this.params.get(handle)?.push(null); return 1n; },
    fpg_params_close: (handle: bigint) => { this.params.delete(handle); return 1n; },
    fpg_query: (_conn: bigint, sql: string, handle: bigint) => this.query(sql, this.params.get(handle)),
    fpg_exec: (_conn: bigint, sql: string) => this.query(sql),
    fpg_ok: (handle: bigint) => this.results.get(handle)?.state === '' ? 1n : 0n,
    fpg_rows: (handle: bigint) => BigInt(this.results.get(handle)?.rows.length ?? 0),
    fpg_affected: (handle: bigint) => BigInt(this.results.get(handle)?.affected ?? 0),
    fpg_value: (handle: bigint, row: bigint, column: bigint) => this.results.get(handle)?.rows[Number(row)]?.[Number(column)] ?? '',
    fpg_state: (handle: bigint) => this.results.get(handle)?.state ?? 'XX000',
    fpg_clear: (handle: bigint) => { this.results.delete(handle); return 1n; },
  };
}
