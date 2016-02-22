{
  'targets': [
    {
      'target_name': 'resampler',
      'include_dirs': [
        "<!(node -e \"require('nan')\")"
      ],
      'sources': [
        'resampler.cc'
      ],
      'libraries': [
        '-lresample'
      ]
    }
  ]
}
