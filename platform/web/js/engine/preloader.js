const Preloader = /** @constructor */ function () { // eslint-disable-line no-unused-vars
	function getTrackedResponse(response, load_status) {
		function onloadprogress(reader, controller) {
			return reader.read().then(function (result) {
				if (load_status.done) {
					return Promise.resolve();
				}
				if (result.value) {
					controller.enqueue(result.value);
					load_status.loaded += result.value.length;
				}
				if (!result.done) {
					return onloadprogress(reader, controller);
				}
				load_status.done = true;
				return Promise.resolve();
			});
		}
		const reader = response.body.getReader();
		return new Response(new ReadableStream({
			start: function (controller) {
				onloadprogress(reader, controller).then(function () {
					controller.close();
				}).catch(function (err) {
					controller.error(err);
				});
			},
		}), { headers: response.headers });
	}

	function loadFetch(file, tracker, fileSize, raw, signal) {
		tracker[file] = {
			total: fileSize || 0,
			loaded: 0,
			done: false
		};
		const init = signal ? { signal: signal } : {};
		
		return fetch(file, init).then(function (response) {
			if (!response.ok) {
				return Promise.reject(new Error(`Failed loading file '${file}'`));
			}
			const tr = getTrackedResponse(response, tracker[file]);
			if (raw) {
				return Promise.resolve(tr);
			}
			return tr.arrayBuffer();
		});
	}

	function retry(func, attempts = 1) {
		function onerror(err) {
			if (attempts <= 1) {
				return Promise.reject(err);
			}
			return new Promise(function (resolve, reject) {
				setTimeout(function () {
					retry(func, attempts - 1).then(resolve).catch(reject);
				}, 1000);
			});
		}
		return func().catch(onerror);
	}

	const DOWNLOAD_ATTEMPTS_MAX = 4;
	const loadingFiles = {};
	const lastProgress = { loaded: 0, total: 0 };
	let progressFunc = null;

	const animateProgress = function () {
		let loaded = 0;
		let total = 0;
		let totalIsValid = true;
		let progressIsFinal = true;

		Object.keys(loadingFiles).forEach(function (file) {
			const stat = loadingFiles[file];
			if (!stat.done) {
				progressIsFinal = false;
			}
			if (!totalIsValid || stat.total === 0) {
				totalIsValid = false;
				total = 0;
			} else {
				total += stat.total;
			}
			loaded += stat.loaded;
		});
		if (loaded !== lastProgress.loaded || total !== lastProgress.total) {
			lastProgress.loaded = loaded;
			lastProgress.total = total;
			if (typeof progressFunc === 'function') {
				const safeTotal = total === 0 ? Math.max(loaded, 1) : total;
				progressFunc(loaded, safeTotal);
			}
		}
		if (!progressIsFinal) {
			requestAnimationFrame(animateProgress);
		}
	};

	this.animateProgress = animateProgress;

	this.setProgressFunc = function (callback) {
		progressFunc = callback;
	};

	this.loadPromise = function (file, fileSize, raw = false) {
		const abortController = new AbortController();
		
		// If the fetch fails, we should abort the hanging controller just in case 
		// it was a timeout or stream stall.
		const func = () => {
			return loadFetch(file, loadingFiles, fileSize, raw, abortController.signal)
				.catch(err => {
					abortController.abort();
					throw err;
				});
		};

		return retry(func, DOWNLOAD_ATTEMPTS_MAX);
	};

	this.preloadedFiles = [];
	this.preload = function (pathOrBuffer, destPath, fileSize) {
		let buffer = null;
		if (typeof pathOrBuffer === 'string') {
			const me = this;
			return this.loadPromise(pathOrBuffer, fileSize).then(function (buf) {
				me.preloadedFiles.push({
					path: destPath || pathOrBuffer,
					buffer: buf,
				});
				return Promise.resolve();
			});
		} else if (pathOrBuffer instanceof ArrayBuffer) {
			buffer = new Uint8Array(pathOrBuffer);
		} else if (ArrayBuffer.isView(pathOrBuffer)) {
			buffer = new Uint8Array(pathOrBuffer.buffer, pathOrBuffer.byteOffset, pathOrBuffer.byteLength);
		}
		if (buffer) {
			this.preloadedFiles.push({
				path: destPath,
				buffer: buffer,
			});
			return Promise.resolve();
		}
		return Promise.reject(new Error('Invalid object for preloading'));
	};
};
