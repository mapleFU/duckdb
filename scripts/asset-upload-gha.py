import json
import os
import sys
import glob
import time
import mimetypes
import urllib.request

api_url = 'https://api.github.com/repos/duckdb/duckdb/'

if (len(sys.argv) < 2):
	print("Usage: [filename1] [filename2] ... ")
	exit(1)

# this essentially should run on release tag builds to fill up release assets and master

repo = os.getenv("GITHUB_REPOSITORY", "")
if repo != "duckdb/duckdb":
	print("Not running on forks. Exiting.")
	exit(0)

ref = os.getenv("GITHUB_REF", '') # this env var is always present just not always used
if ref == 'refs/heads/master':
	tag = 'master-builds'
elif ref.startswith('refs/tags/'):
	tag = ref.replace('refs/tags/', '')
else:
	print("Not running on branches. Exiting.")
	exit(0)


print("Running on tag %s" % tag)


token = os.getenv("GH_TOKEN", "")
if token == "":
	raise ValueError('need a GitHub token in GH_TOKEN')

def gh_api(suburl, filename='', method='GET'):
	url = api_url + suburl
	headers = {
		"Content-Type": "application/json",
		'Authorization': 'token ' + token
	}

	body_data = b''

	print(f'GH API URL: "{url}" Filename: "{filename}" Method: "{method}"')
	if len(filename) > 0:
		method = 'POST'
		body_data = open(filename, 'rb')

		mime_type = mimetypes.guess_type(local_filename)[0]
		if mime_type is None:
			mime_type = "application/octet-stream"
		headers["Content-Type"] = mime_type
		headers["Content-Length"] = os.path.getsize(local_filename)

		url = suburl # cough

	req = urllib.request.Request(url, body_data, headers)
	req.get_method = lambda: method
	timeout = 1
	nretries = 10
	raw_resp = None
	for i in range(nretries+1):
		success = True
		try:
			raw_resp = urllib.request.urlopen(req).read().decode()
		except Exception as e:
			print(e)
			success = False
		if success:
			break
		print(f"Failed upload, retrying in {timeout} seconds... ({i}/{nretries})")
		time.sleep(timeout)
		timeout = timeout * 2
	if not success:
		raise Exception("Failed to open URL " + url)

	if (method != 'DELETE'):
		return json.loads(raw_resp)
	else:
		return {}

# check if tag exists
resp = gh_api('git/ref/tags/%s' % tag)
if 'object' not in resp or 'sha' not in resp['object'] : # or resp['object']['sha'] != sha
	raise ValueError('tag %s not found' % tag)

resp = gh_api('releases/tags/%s' % tag)
if 'id' not in resp or 'upload_url' not in resp:
	raise ValueError('release does not exist for tag ' % tag)

# double-check that release exists and has correct sha
# disabled to not spam people watching releases
# if 'id' not in resp or 'upload_url' not in resp or 'target_commitish' not in resp or resp['target_commitish'] != sha:
# 	raise ValueError('release does not point to requested commit %s' % sha)

# TODO this could be a paged response!
assets = gh_api('releases/%s/assets' % resp['id'])

upload_url = resp['upload_url'].split('{')[0] # gah
files = sys.argv[1:]
for filename in files:
	if '=' in filename:
		parts = filename.split("=")
		asset_filename = parts[0]
		paths = glob.glob(parts[1])
		if len(paths) != 1:
			raise ValueError("Could not find file for pattern %s" % parts[1])
		local_filename = paths[0]
	else:
		asset_filename = os.path.basename(filename)
		local_filename = filename

	# delete if present
	for asset in assets:
		if asset['name'] == asset_filename:
			gh_api('releases/assets/%s' % asset['id'], method='DELETE')

	resp = gh_api(upload_url + '?name=%s' % asset_filename, filename=local_filename)
	if 'id' not in resp:
		raise ValueError('upload failed :/ ' + str(resp))
	print("%s -> %s" % (local_filename, resp['browser_download_url']))
