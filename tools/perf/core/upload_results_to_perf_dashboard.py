#!/usr/bin/env vpython
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file was copy-pasted over from:
# //build/scripts/slave/upload_perf_dashboard_results.py
# with sections copied from:
# //build/scripts/slave/slave_utils.py

import json
import optparse
import os
import re
import shutil
import sys
import tempfile
import time
import urllib

from core import results_dashboard


RESULTS_LINK_PATH = '/report?masters=%s&bots=%s&tests=%s&rev=%s'


def _GetMainRevision(commit_pos):
  """Return revision to use as the numerical x-value in the perf dashboard.
  This will be used as the value of "rev" in the data passed to
  results_dashboard.SendResults.
  This function returns the value of "got_revision_cp" in build properties.
  """
  return int(re.search(r'{#(\d+)}', commit_pos).group(1))


def _GetDashboardJson(options):
  main_revision = _GetMainRevision(options.got_revision_cp)
  revisions = _GetPerfDashboardRevisionsWithProperties(
    options.got_webrtc_revision, options.got_v8_revision,
    options.git_revision, main_revision)
  reference_build = 'reference' in options.name
  stripped_test_name = options.name.replace('.reference', '')
  results = {}
  print 'Opening results file %s' % options.results_file
  with open(options.results_file) as f:
    results = json.load(f)
  dashboard_json = {}
  if 'charts' not in results:
    # These are legacy results.
    # pylint: disable=redefined-variable-type
    dashboard_json = results_dashboard.MakeListOfPoints(
      results, options.configuration_name, stripped_test_name,
      options.buildername, options.buildnumber, {},
      options.perf_dashboard_machine_group,
      revisions_dict=revisions)
  else:
    dashboard_json = results_dashboard.MakeDashboardJsonV1(
      results,
      revisions, stripped_test_name, options.configuration_name,
      options.buildername, options.buildnumber,
      {}, reference_build,
      perf_dashboard_machine_group=options.perf_dashboard_machine_group)
  return dashboard_json


def _GetDashboardHistogramData(options):
  revisions = {
      '--chromium_commit_positions': _GetMainRevision(options.got_revision_cp),
      '--chromium_revisions': options.git_revision
  }

  if options.got_webrtc_revision:
    revisions['--webrtc_revisions'] = options.got_webrtc_revision
  if options.got_v8_revision:
    revisions['--v8_revisions'] = options.got_v8_revision

  is_reference_build = 'reference' in options.name
  stripped_test_name = options.name.replace('.reference', '')

  # 30 MB max_bytes was chosen to be large enough that we rarely have to break
  # up perf data into chunks since it is slow (crbug.com/947035) but small
  # enough that we don't cause crbug.com/909961.
  max_bytes = 30 << 20
  output_dir = tempfile.mkdtemp()

  try:
    begin_time = time.time()
    results_dashboard.MakeHistogramSetWithDiagnostics(
        histograms_file=options.results_file, test_name=stripped_test_name,
        bot=options.configuration_name, buildername=options.buildername,
        buildnumber=options.buildnumber,
        project=options.project, buildbucket=options.buildbucket,
        revisions_dict=revisions, is_reference_build=is_reference_build,
        perf_dashboard_machine_group=options.perf_dashboard_machine_group,
        output_dir=output_dir,
        max_bytes=max_bytes)
    end_time = time.time()
    print 'Duration of adding diagnostics for %s: %d seconds' % (
        stripped_test_name, end_time - begin_time)

    # Read all batch files from output_dir.
    dashboard_jsons = []
    for basename in os.listdir(output_dir):
      with open(os.path.join(output_dir, basename)) as f:
        dashboard_jsons.append(json.load(f))

    return dashboard_jsons
  finally:
    shutil.rmtree(output_dir)


def _CreateParser():
  # Parse options
  parser = optparse.OptionParser()
  parser.add_option('--name')
  parser.add_option('--results-file')
  parser.add_option('--output-json-file')
  parser.add_option('--got-revision-cp')
  parser.add_option('--configuration-name')
  parser.add_option('--results-url')
  parser.add_option('--perf-dashboard-machine-group')
  parser.add_option('--project')
  parser.add_option('--buildbucket')
  parser.add_option('--buildername')
  parser.add_option('--buildnumber')
  parser.add_option('--got-webrtc-revision')
  parser.add_option('--got-v8-revision')
  parser.add_option('--git-revision')
  parser.add_option('--output-json-dashboard-url')
  parser.add_option('--send-as-histograms', action='store_true')
  parser.add_option('--service-account-file', default=None)
  return parser


def main(args):
  parser = _CreateParser()
  options, extra_args = parser.parse_args(args)

  # Validate options.
  if extra_args:
    parser.error('Unexpected command line arguments')
  if not options.configuration_name or not options.results_url:
    parser.error('configuration_name and results_url are required.')

  service_account_file = options.service_account_file

  if not options.perf_dashboard_machine_group:
    print 'Error: Invalid perf dashboard machine group'
    return 1

  if not options.send_as_histograms:
    dashboard_json = _GetDashboardJson(options)
    dashboard_jsons = []
    if dashboard_json:
      dashboard_jsons.append(dashboard_json)
  else:
    dashboard_jsons = _GetDashboardHistogramData(options)

    # The HistogramSet might have been batched if it would be too large to
    # upload together. It's safe to concatenate the batches in order to write
    # output_json_file.
    # TODO(crbug.com/918208): Use a script in catapult to merge dashboard_jsons.
    dashboard_json = sum(dashboard_jsons, [])

  if options.output_json_file:
    json.dump(dashboard_json, options.output_json_file,
        indent=4, separators=(',', ': '))

  if dashboard_jsons:
    if options.output_json_dashboard_url:
      # Dump dashboard url to file.
      dashboard_url = GetDashboardUrl(options.name,
          options.configuration_name, options.results_url,
          options.got_revision_cp,
          options.perf_dashboard_machine_group)
      with open(options.output_json_dashboard_url, 'w') as f:
        json.dump(dashboard_url if dashboard_url else '', f)

    for batch in dashboard_jsons:
      if not results_dashboard.SendResults(
          batch,
          options.name,
          options.results_url,
          send_as_histograms=options.send_as_histograms,
          service_account_file=service_account_file):
        return 1
  else:
    # The upload didn't fail since there was no data to upload.
    print 'Warning: No perf dashboard JSON was produced.'
  return 0

if __name__ == '__main__':
  sys.exit(main((sys.argv[1:])))


def GetDashboardUrl(name, configuration_name, results_url,
    got_revision_cp, perf_dashboard_machine_group):
  """Optionally writes the dashboard url to a file
    and returns a link to the dashboard.
  """
  name = name.replace('.reference', '')
  dashboard_url = results_url + RESULTS_LINK_PATH % (
      urllib.quote(perf_dashboard_machine_group),
      urllib.quote(configuration_name),
      urllib.quote(name),
      _GetMainRevision(got_revision_cp))

  return dashboard_url


def _GetPerfDashboardRevisionsWithProperties(
    got_webrtc_revision, got_v8_revision, git_revision, main_revision,
    point_id=None):
  """Fills in the same revisions fields that process_log_utils does."""

  versions = {}
  versions['rev'] = main_revision
  versions['webrtc_git'] = got_webrtc_revision
  versions['v8_rev'] = got_v8_revision
  versions['git_revision'] = git_revision
  versions['point_id'] = point_id
  # There are a lot of "bad" revisions to check for, so clean them all up here.
  for key in versions.keys():
    if not versions[key] or versions[key] == 'undefined':
      del versions[key]
  return versions
