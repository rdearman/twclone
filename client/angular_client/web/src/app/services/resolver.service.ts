import { Injectable } from '@angular/core';
import { StateService } from './state.service';
import { firstValueFrom } from 'rxjs';

@Injectable({
  providedIn: 'root'
})
export class ResolverService {

  constructor(private state: StateService) { }

  async resolve(obj: any): Promise<any> {
    if (typeof obj === 'string') {
      return await this.resolveString(obj);
    } else if (Array.isArray(obj)) {
      const res = [];
      for (const item of obj) {
        res.push(await this.resolve(item));
      }
      return res;
    } else if (obj !== null && typeof obj === 'object') {
      const res: any = {};
      for (const key in obj) {
        res[key] = await this.resolve(obj[key]);
      }
      return res;
    }
    return obj;
  }

  async resolveString(val: string): Promise<any> {
    let result = val;

    // 1. Resolve CTX
    const ctxRegex = /<ctx:([^>]+)>/g;
    let match;
    // We use a temp string for replacement to avoid regex index issues if lengths change
    // but here we just do simple replace since we might want to return non-string if it's the only thing.
    
    const ctxMatches = Array.from(val.matchAll(ctxRegex));
    for (const m of ctxMatches) {
        const path = m[1];
        const resolved = this.resolvePath(path);
        // If the whole string is just this one ctx, return the actual object/value
        if (val === m[0]) return resolved;
        result = result.replace(m[0], String(resolved ?? ''));
    }

    // 2. Resolve PROMPT
    const promptRegex = /<prompt:([^>]+)>/g;
    const promptMatches = Array.from(result.matchAll(promptRegex));
    for (const m of promptMatches) {
        const label = m[1];
        const userInput = await this.askUser(label);
        if (result === m[0]) return userInput;
        result = result.replace(m[0], userInput);
    }

    return result;
  }

  private resolvePath(path: string): any {
    const parts = path.split('.');
    let cur = this.state.gameState;
    for (const part of parts) {
      if (cur == null) return undefined;
      cur = cur[part];
    }
    return cur;
  }

  private askUser(label: string): Promise<string> {
    return new Promise((resolve) => {
      this.state.prompt$.next({ label, resolve });
    });
  }
}