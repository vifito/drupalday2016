<?php

/**
 * @file
 */

/**
 * Implements hook_drush_command().
 */
function drupalday_drush_command() {

  $items['dd-import'] = array(
    'callback' => '_drupalday_import',
    'description' => 'Importar contenidos en ejemplo Drupalday.',
    'aliases' => array('ddi'),
    'arguments' => array(
      'input' => 'Fichero JSON con datos a cargar.',
    ),
  );

  return $items;
}

/**
 * Importar as páxinas de calquera sección
 *
 *   Scrapy:
 *     scrapy crawl
 *   Drush (path/to/drupal):
 *     drush @local ddi --assets=./assets ./paxinas.json
 *
 * @param $input
 * @param $assets
 * @return bool
 */
function _drupalday_import($input, $assets) {

  if (!file_exists($input)) {
    return drush_set_error('FILE_NOT_FOUND', sprintf('El fichero %s no existe', $input));
  }

  if(!is_dir($assets)) {
    return drush_set_error('ASSETS_NOT_FOUND', sprintf('El directorio de assets %s no existe', $assets));
  }

  $data = file_get_contents($input);
  $data = json_decode($data, True);

  //$data = [$data[0]]; // TODO: Probas

  if (!is_array($data)) {
    return drush_set_error('IMPORT_DATA_CORRUPTED', 'Los datos importados están corruptos');
  }

  // Definir por se existen limitacións no servidor
  ini_set('memory_limit', '256M');

  /** @var \Drupal\Core\Path\AliasStorage $aliasStorage */
  $aliasStorage = \Drupal::service('path.alias_storage');

  /** @var \Drupal\Core\Url $base_url */
  $base_url = \Drupal\Core\Url::fromUri('base:/', ['absolute' => FALSE]);

  $weight = 0;

  // Crear os Nodos contidos
  foreach($data as $item) {
    $link = preg_replace('@http:\/\/(www\.)?[a-z0-9\-_]+\.[a-z]{2,6}@', '', $item['url']);
    $langcode = $item['lang'];

    if (!$aliasStorage->aliasExists($link, $langcode)) {
      // <editor-fold desc="values">
      $values = [
        'type'     => 'usc_front_page_general',
        'title'    => $item['titulo'],
        'langcode' => $langcode,
        'status'   => 1,
        'promote'  => 0,
        'sticky'   => 0,
        'uid'      => 1,

        'body'     => [
          'value'   => $item['contidos'],
          'summary' => '',
          'format'  => 'basic_html', // full_html
        ],

        'created' => \time(),
        'changed' => \time(),

        'field_fe_description' => $item['description'],
      ];
      // </editor-fold>

      // <editor-fold desc="field_fe_image">
      if(isset($item['images']) && count($item['images']) > 0) {
        $image_id = [];
        $alt = [];

        foreach($item['images'] as $img) {
          /**
           * assets
           * ├── files
           * │   └── full
           * └── images
           *     └── full
           */
          // http://www.usc.es/export/sites/default/gl/info_xeral/imaxeusc/imaxes/negat_azul.gif
          $path = preg_replace('/(http:\/\/(www\.)?usc\.es)?\/export\d*\/sites\/(default|webinstitucional)\/(gl|es|en)\//', '', $img['url']);
          $path = 'public://' . $path;

          $imagename = realpath($assets . '/images/' . $img['path']);
          if(!file_exists($imagename)) {
            drush_log('Imaxe ' . $imagename . ' non existe.', 'error');
            continue;
          }

          $image_stream = file_get_contents($imagename);

          $dir = dirname($path);
          if(!file_prepare_directory($dir)) {
            drupal_mkdir($dir, NULL, True);
          }

          if(strlen($path) > 255) {
            $parts = pathinfo($path);

            $dirname   = $parts['dirname'] . '/';
            $extension = $parts['extension'];
            $filename  = $parts['filename'];

            $path = $dirname . substr($filename, 0, 255-(strlen($dirname) + strlen($extension) + 1)) . '.' . $extension;
            drush_log('Imaxe path truncado: ' . $path . '.', 'warning');
          }

          $file = file_save_data($image_stream, $path, FILE_EXISTS_REPLACE);

          // Buscar o texto alternativo
          $alt = null;
          if(is_array($item['image_alts'])) {
            foreach($item['image_alts'] as $img_alt) {
              if($img_alt['url'] === $img['url']) {
                $alt = $img_alt['alt'];
              }
            }
          }

          $alternativeText = $alt !== null? $alt: t('Imaxe');
          $image_id[] = [
            'target_id' => $file->id(),
            'alt' => $alternativeText,
          ];

          $wrapper = \Drupal::service('stream_wrapper_manager')->getViaUri($path);
          $externalUrl = $wrapper->getExternalUrl();

          $uri = preg_replace('/http:\/\/www\.usc\.es/', '', $img['url']);

          // Remprazar nos contidos
          $values['body']['value'] = preg_replace_callback(
            '@<img(?P<pre>[^>]*?)src=["\']' . preg_quote($uri) . '["\'](?P<post>[^>]*?)>@',
            function($matches) use($externalUrl, $alternativeText) {
              $attrs = preg_replace('/ alt=[\"\'].*?[\"\']/', '', $matches['pre'] . ' ' . $matches['post']);
              $img = '<img src="' . $externalUrl . '" alt="' . $alternativeText . '" '. $attrs . ' />';
              return preg_replace('/[ ]{2,}/', ' ', $img);
            },
            $values['body']['value']
          );
        }

        $values['field_fe_image'] = $image_id;
      }
      // </editor-fold>


      $node = \Drupal\node\Entity\Node::create($values);
      $node->save();

      try {
        $aliasStorage->save('/node/' . $node->get('nid')
            ->get(0)
            ->getString(), $link, $langcode);

        drush_log('Nova ' . $item['titulo'] . ' (' . $link . ')', 'success');
      } catch(\Drupal\Core\TypedData\Exception\MissingDataException $ex) {
        drush_log('Nova ' . $item['titulo'] . ' alias (' . $link . ') NON establecido correctamente.', 'error');
      }
    }

  }

  return true;
}
