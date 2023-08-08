import { StartStreaming, tags } from "@vlcn.io/partykit-common";
import { DB } from "../config.js";
import { Transport } from "../transport/Transport.js";

export default class OutboundStream {
  readonly #db;
  readonly #transport;
  #lastSent: readonly [bigint, number] | null = null;
  #excludeSites: readonly Uint8Array[] = [];
  #localOnly: boolean = false;
  readonly #disposer;

  constructor(db: DB, transport: Transport) {
    this.#db = db;
    this.#transport = transport;
    this.#disposer = this.#db.onChange(this.#dbChanged);
  }

  async startStreaming(msg: StartStreaming) {
    this.#lastSent = msg.since;
    this.#excludeSites = msg.excludeSites;
    this.#localOnly = msg.localOnly;
    // initial kickoff so we don't wait for a db change event
    this.#dbChanged();
  }

  async resetStream(msg: StartStreaming) {
    this.startStreaming(msg);
  }

  // TODO: ideally we get throttle information from signals from the rest of the system.
  // Should throttle be here or something that the user would be expected to set up?
  // Ideally we can let them control it so they can make the responsiveness tradeoffs they want.
  #dbChanged = async () => {
    if (this.#lastSent == null) {
      return;
    }

    // save off last sent so we can detect a reset that happened while pulling changes.
    const lastSent = this.#lastSent;

    const changes = await this.#db.pullChangeset(
      lastSent,
      this.#excludeSites,
      this.#localOnly
    );
    if (lastSent != this.#lastSent) {
      // we got reset. Abort.
      return;
    }

    if (changes.length == 0) {
      return;
    }
    const lastChange = changes[changes.length - 1];
    this.#lastSent = [lastChange[5], 0];

    console.log(`Sending ${changes.length} changes since ${this.#lastSent}`);

    try {
      await this.#transport.sendChanges({
        _tag: tags.Changes,
        changes,
        sender: this.#db.siteid,
        since: lastSent,
      });
    } catch (e) {
      this.#lastSent = lastSent;
      throw e;
    }
  };

  // stop listening to the base DB
  stop() {
    this.#disposer();
  }
}
